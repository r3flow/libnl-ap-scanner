/**
 * Code applied from:
 * - libnl sources LGPL2.1 https://www.infradead.org/~tgr/libnl/
 * - example code from Python libnl port (LGPL2.1):
 *	 https://github.com/Robpol86/libnl/blob/master/example_c/scan_access_points.c
 * - as well as iw(8) source code (MIT).
 *   https://git.sipsolutions.net/iw.git/tree/scan.c
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * This program uses the scanning features of wifi hardware to discover nearby
 * wifi access points and prints information about them. Due to libnl / nl80211
 * being used, this program only works on somewhat newer wifi drivers.
 * Only root can perform scanning due to NL80211_CMD_TRIGGER_SCAN being limited
 * to root by default.
 *
 * This changed version of the example program scan_access_points.c addresses several
 * errors that were not handled, scans for more information, rearranges the code
 * in a cleaner format and improves on the documentation. In addition, several memory
 * leaks with allocated libnl resources are handled.
 */

#include <errno.h>
#include <ctype.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <memory>
#include <stdio.h>

#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(ar[0]))

// These are from iw source code, and they related to parsing BSS capabilities
#define WLAN_CAPABILITY_ESS                 (1<<0)
#define WLAN_CAPABILITY_IBSS                (1<<1)
#define WLAN_CAPABILITY_CF_POLLABLE         (1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST     (1<<3)
#define WLAN_CAPABILITY_PRIVACY             (1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE      (1<<5)
#define WLAN_CAPABILITY_PBCC                (1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY     (1<<7)
#define WLAN_CAPABILITY_SPECTRUM_MGMT       (1<<8)
#define WLAN_CAPABILITY_QOS                 (1<<9)
#define WLAN_CAPABILITY_SHORT_SLOT_TIME     (1<<10)
#define WLAN_CAPABILITY_APSD                (1<<11)
#define WLAN_CAPABILITY_RADIO_MEASURE       (1<<12)
#define WLAN_CAPABILITY_DSSS_OFDM           (1<<13)
#define WLAN_CAPABILITY_DEL_BACK            (1<<14)
#define WLAN_CAPABILITY_IMM_BACK            (1<<15)
/* DMG (60gHz) 802.11ad */
/* type - bits 0..1 */
#define WLAN_CAPABILITY_DMG_TYPE_MASK       (3<<0)

#define WLAN_CAPABILITY_DMG_TYPE_IBSS       (1<<0) /* Tx by: STA */
#define WLAN_CAPABILITY_DMG_TYPE_PBSS       (2<<0) /* Tx by: PCP */
#define WLAN_CAPABILITY_DMG_TYPE_AP         (3<<0) /* Tx by: AP */

#define WLAN_CAPABILITY_DMG_CBAP_ONLY       (1<<2)
#define WLAN_CAPABILITY_DMG_CBAP_SOURCE     (1<<3)
#define WLAN_CAPABILITY_DMG_PRIVACY         (1<<4)
#define WLAN_CAPABILITY_DMG_ECPAC           (1<<5)

#define WLAN_CAPABILITY_DMG_SPECTRUM_MGMT   (1<<8)
#define WLAN_CAPABILITY_DMG_RADIO_MEASURE   (1<<12)

// These are from iw source code, and they related to parsing authentication suites
const unsigned char ms_oui[3] = { 0x00, 0x50, 0xf2 };
const unsigned char ieee80211_oui[3]   = { 0x00, 0x0f, 0xac };
const unsigned char wfa_oui[3] = { 0x50, 0x6f, 0x9a };

// global variable that contains the MAC address for the current scan result,
// used to make sure every print contains clarification for which MAC the data is.
char current_mac[20];

const char* DISCOVER_STR = "AP_DISCOVERED,";
const char* DATA_STR = "AP_DATA,";
const char* BSS_SECTION = "BSS";

inline void dataline(const char* section_name = NULL) {
	printf("%s%s,%s,", DATA_STR, current_mac, section_name != NULL ? section_name : BSS_SECTION);
}

static void sep_if_not_first(bool *first, const char* separator = ",")
{
	if (!*first)
		printf("%s", separator);
	else
		*first = false;
}

struct init_scan_results {
	int done;
	int aborted;
};

struct print_ies_data {
	unsigned char *ie;
	int ielen;
};

// Error callback
int error_handler(struct sockaddr_nl* nla, struct nlmsgerr* err, void* arg) {
	int* ret = (int*)arg;
	*ret = err->error;
	return NL_STOP;
}

// Callback for NL_CB_FINISH
int finish_handler(struct nl_msg* msg, void* arg) {
	int* ret = (int*)arg;
	*ret = 0;
	return NL_SKIP;
}

// Callback for NL_CB_ACK
int ack_handler(struct nl_msg *msg, void* arg) {
	int* ret = (int*)arg;
	*ret = 0;
	return NL_STOP;
}

// Callback for NL_CB_SEQ_CHECK
int no_seq_check(struct nl_msg* msg, void* arg) {
	return NL_OK;
}

// From http://git.kernel.org/cgit/linux/kernel/git/jberg/iw.git/tree/util.c
void mac_addr_n2a(char* mac_addr, unsigned char* arg) {

	int i, l;
	l = 0;
	for (i = 0; i < 6; i++) {
		if (i == 0) {
			sprintf(mac_addr+l, "%02x", arg[i]);
			l += 2;
		} else {
			sprintf(mac_addr+l, ":%02x", arg[i]);
			l += 3;
		}
	}
}

static void print_capa_dmg(__u16 capa, bool* first)
{
	switch (capa & WLAN_CAPABILITY_DMG_TYPE_MASK) {
		case WLAN_CAPABILITY_DMG_TYPE_AP: {
			sep_if_not_first(first);
			printf("DMG_ESS");
			break;
		}
		case WLAN_CAPABILITY_DMG_TYPE_PBSS: {
			sep_if_not_first(first);
			printf("DMG_PCP");
			break;
		}
		case WLAN_CAPABILITY_DMG_TYPE_IBSS: {
			sep_if_not_first(first);
			printf("DMG_IBSS");
			break;
		}
	}

	if (capa & WLAN_CAPABILITY_DMG_CBAP_ONLY){
		sep_if_not_first(first);
		printf("CBAP_Only");
	}
	if (capa & WLAN_CAPABILITY_DMG_CBAP_SOURCE){
		sep_if_not_first(first);
		printf("CBAP_Src");
	}
	if (capa & WLAN_CAPABILITY_DMG_PRIVACY){
		sep_if_not_first(first);
		printf("Privacy");
	}
	if (capa & WLAN_CAPABILITY_DMG_ECPAC){
		sep_if_not_first(first);
		printf("ECPAC");
	}
	if (capa & WLAN_CAPABILITY_DMG_SPECTRUM_MGMT){
		sep_if_not_first(first);
		printf("SpectrumMgmt");
	}
	if (capa & WLAN_CAPABILITY_DMG_RADIO_MEASURE){
		sep_if_not_first(first);
		printf("RadioMeasure");
	}
}

static void print_capa_non_dmg(__u16 capa, bool* first)
{
	if (capa & WLAN_CAPABILITY_ESS){
		sep_if_not_first(first);
		printf("ESS");
	}
	if (capa & WLAN_CAPABILITY_IBSS){
		sep_if_not_first(first);
		printf("IBSS");
	}
	if (capa & WLAN_CAPABILITY_CF_POLLABLE){
		sep_if_not_first(first);
		printf("CfPollable");
	}
	if (capa & WLAN_CAPABILITY_CF_POLL_REQUEST){
		sep_if_not_first(first);
		printf("CfPollReq");
	}
	if (capa & WLAN_CAPABILITY_PRIVACY){
		sep_if_not_first(first);
		printf("Privacy");
	}
	if (capa & WLAN_CAPABILITY_SHORT_PREAMBLE){
		sep_if_not_first(first);
		printf("ShortPreamble");
	}
	if (capa & WLAN_CAPABILITY_PBCC){
		sep_if_not_first(first);
		printf("PBCC");
	}
	if (capa & WLAN_CAPABILITY_CHANNEL_AGILITY){
		sep_if_not_first(first);
		printf("ChannelAgility");
	}
	if (capa & WLAN_CAPABILITY_SPECTRUM_MGMT){
		sep_if_not_first(first);
		printf("SpectrumMgmt");
	}
	if (capa & WLAN_CAPABILITY_QOS){
		sep_if_not_first(first);
		printf("QoS");
	}
	if (capa & WLAN_CAPABILITY_SHORT_SLOT_TIME){
		sep_if_not_first(first);
		printf("ShortSlotTime");
	}
	if (capa & WLAN_CAPABILITY_APSD){
		sep_if_not_first(first);
		printf("APSD");
	}
	if (capa & WLAN_CAPABILITY_RADIO_MEASURE){
		sep_if_not_first(first);
		printf("RadioMeasure");
	}
	if (capa & WLAN_CAPABILITY_DSSS_OFDM){
		sep_if_not_first(first);
		printf("DSSS-OFDM");
	}
	if (capa & WLAN_CAPABILITY_DEL_BACK){
		sep_if_not_first(first);
		printf("DelayedBACK");
	}
	if (capa & WLAN_CAPABILITY_IMM_BACK){
		sep_if_not_first(first);
		printf("ImmediateBACK");
	}
}

static const char * wifi_wps_dev_passwd_id(uint16_t id)
{
	switch (id) {
	case 0:
		return "Default (PIN)";
	case 1:
		return "User-specified";
	case 2:
		return "Machine-specified";
	case 3:
		return "Rekey";
	case 4:
		return "PushButton";
	case 5:
		return "Registrar-specified";
	default:
		return "??";
	}
}

static void print_wifi_wps(const uint8_t type, uint8_t len, const uint8_t *data,
			   struct print_ies_data *ie_buffer, const char* section_name)
{
	__u16 subtype, sublen;

	while (len >= 4) {
		subtype = (data[0] << 8) + data[1];
		sublen = (data[2] << 8) + data[3];
		if (sublen > len - 4)
			break;

		switch (subtype) {
		case 0x104a:
			if (sublen < 1) break;

			dataline(section_name);
			printf("version:%d.%d\n", data[4] >> 4, data[4] & 0xF);
			break;
		case 0x1011:
			dataline(section_name);
			printf("device name:%.*s\n", sublen, data + 4);
			break;
		case 0x1012: {
			uint16_t id;
			if (sublen != 2) break;
			
			id = data[4] << 8 | data[5];
			dataline(section_name);
			printf("device password id:%u (%s)\n", id, wifi_wps_dev_passwd_id(id));
			break;
		}
		case 0x1021:
			dataline(section_name);
			printf("manufacturer:%.*s\n", sublen, data + 4);
			break;
		case 0x1023:
			dataline(section_name);
			printf("model:%.*s\n", sublen, data + 4);
			break;
		case 0x1024:
			dataline(section_name);
			printf("model Number:%.*s\n", sublen, data + 4);
			break;
		case 0x103b: {
			__u8 val;

			if (sublen < 1) break;
			
			val = data[4];
			dataline(section_name);
			printf("response type:%d%s\n", val, val == 3 ? " (AP)" : "");
			break;
		}
		case 0x103c: {
			__u8 val;

			if (sublen < 1) break;

			val = data[4];
			dataline(section_name);
			printf("rf bands:0x%x\n", val);
			break;
		}
		case 0x1041: {
			__u8 val;

			if (sublen < 1) break;

			val = data[4];
			dataline(section_name);
			printf("selected registrar:0x%x\n", val);
			break;
		}
		case 0x1042:
			dataline(section_name);
			printf("serial number:%.*s\n", sublen, data + 4);
			break;
		case 0x1044: {
			__u8 val;

			if (sublen < 1) break;

			val = data[4];
			dataline(section_name);
			printf("wi-fi protected setup state:%d%s%s\n",
			       val,
			       val == 1 ? " (Unconfigured)" : "",
			       val == 2 ? " (Configured)" : "");
			break;
		}
		case 0x1047:
			if (sublen != 16) break;

			dataline(section_name);
			printf("uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-"
				"%02x%02x-%02x%02x%02x%02x%02x%02x\n",
				data[4], data[5], data[6], data[7],
				data[8], data[9], data[10], data[11],
				data[12], data[13], data[14], data[15],
				data[16], data[17], data[18], data[19]);
			break;
		case 0x1049:
			if (sublen == 6 &&
			    data[4] == 0x00 &&
			    data[5] == 0x37 &&
			    data[6] == 0x2a &&
			    data[7] == 0x00 &&
			    data[8] == 0x01) {
				uint8_t v2 = data[9];
				dataline(section_name);
				printf("version2:%d.%d\n", v2 >> 4, v2 & 0xf);
			}
			break;
		case 0x1054: {
			if (sublen != 8) break;

			dataline(section_name);
			printf("primary device type:"
			       "%u-%02x%02x%02x%02x-%u\n",
			       data[4] << 8 | data[5],
			       data[6], data[7], data[8], data[9],
			       data[10] << 8 | data[11]);
			break;
		}
		case 0x1057: {
			__u8 val;

			if (sublen < 1) break;

			val = data[4];
			dataline(section_name);
			printf("ap setup locked:0x%.2x\n", val);
			break;
		}
		case 0x1008:
		case 0x1053: {
			__u16 meth;
			bool comma;

			if (sublen < 2) break;

			meth = (data[4] << 8) + data[5];
			comma = false;
			dataline(section_name);
			printf("%sconfig methods:",
			       subtype == 0x1053 ? "selected registrar ": "");
#define T(bit, name) do {		\
	if (meth & (1<<bit)) {		\
		if (comma)		\
			printf(",");	\
		comma = true;		\
		printf("%s",name);	\
	} } while (0)
			T(0, "USB");
			T(1, "Ethernet");
			T(2, "Label");
			T(3, "Display");
			T(4, "Ext. NFC");
			T(5, "Int. NFC");
			T(6, "NFC Intf.");
			T(7, "PBC");
			T(8, "Keypad");
			printf("\n");
			break;
#undef T
		}
		default: {
			break;
		}
		}

		data += sublen + 4;
		len -= sublen + 4;
	}
}

void print_ssid(const uint8_t type, uint8_t len, const uint8_t *data,
	struct print_ies_data *ie_buffer, const char* section_name) {

	int i;

	dataline();
	printf("ssid:");
	for (i = 0; i < len; i++) {
		if (isprint(data[i]) && data[i] != ' ' && data[i] != '\\') {
			printf("%c", data[i]);
		} else if (data[i] == ' ' && (i != 0 && i != len -1)) {
			printf(" ");
		} else {
			printf("\\x%.2x", data[i]);
		}
	}
	printf("\n");
}

void print_auth(const uint8_t *data) {

	// This is copied from iw sources, and I have no idea how this works
	// There's a lot of magic numbers going around.

	if (memcmp(data, ms_oui, 3) == 0) {
		switch (data[3]) {
		case 1:
			printf("IEEE 802.1X");
			break;
		case 2:
			printf("PSK");
			break;
		default:
			printf("%.02x-%.02x-%.02x:%d", data[0], data[1] ,data[2], data[3]);
			break;
		}
	} else if (memcmp(data, ieee80211_oui, 3) == 0) {
		switch (data[3]) {
		case 1:
			printf("IEEE 802.1X");
			break;
		case 2:
			printf("PSK");
			break;
		case 3:
			printf("FT/IEEE 802.1X");
			break;
		case 4:
			printf("FT/PSK");
			break;
		case 5:
			printf("IEEE 802.1X/SHA-256");
			break;
		case 6:
			printf("PSK/SHA-256");
			break;
		case 7:
			printf("TDLS/TPK");
			break;
		case 8:
			printf("SAE");
			break;
		case 9:
			printf("FT/SAE");
			break;
		case 11:
			printf("IEEE 802.1X/SUITE-B");
			break;
		case 12:
			printf("IEEE 802.1X/SUITE-B-192");
			break;
		case 13:
			printf("FT/IEEE 802.1X/SHA-384");
			break;
		case 14:
			printf("FILS/SHA-256");
			break;
		case 15:
			printf("FILS/SHA-384");
			break;
		case 16:
			printf("FT/FILS/SHA-256");
			break;
		case 17:
			printf("FT/FILS/SHA-384");
			break;
		case 18:
			printf("OWE");
			break;
		default:
			printf("%.02x-%.02x-%.02x:%d", data[0], data[1] ,data[2], data[3]);
			break;
		}
	} else if (memcmp(data, wfa_oui, 3) == 0) {
		switch (data[3]) {
		case 1:
			printf("OSEN");
			break;
		case 2:
			printf("DPP");
			break;
		default:
			printf("%.02x-%.02x-%.02x:%d", data[0], data[1] ,data[2], data[3]);
			break;
		}
	} else {
		printf("%.02x-%.02x-%.02x:%d", data[0], data[1] ,data[2], data[3]);
	}
}

// Copied from iw sources, no idea what the magic values are
static void print_cipher(const uint8_t *data) {

	if (memcmp(data, ms_oui, 3) == 0) {
		switch (data[3]) {
		case 0:
			printf("Use group cipher suite");
			break;
		case 1:
			printf("WEP-40");
			break;
		case 2:
			printf("TKIP");
			break;
		case 4:
			printf("CCMP");
			break;
		case 5:
			printf("WEP-104");
			break;
		default:
			printf("%.02x-%.02x-%.02x:%d", data[0], data[1] ,data[2], data[3]);
			break;
		}
	} else if (memcmp(data, ieee80211_oui, 3) == 0) {
		switch (data[3]) {
		case 0:
			printf("Use group cipher suite");
			break;
		case 1:
			printf("WEP-40");
			break;
		case 2:
			printf("TKIP");
			break;
		case 4:
			printf("CCMP");
			break;
		case 5:
			printf("WEP-104");
			break;
		case 6:
			printf("AES-128-CMAC");
			break;
		case 7:
			printf("NO-GROUP");
			break;
		case 8:
			printf("GCMP");
			break;
		default:
			printf("%.02x-%.02x-%.02x:%d", data[0], data[1] ,data[2], data[3]);
			break;
		}
	} else {
		printf("%.02x-%.02x-%.02x:%d", data[0], data[1] ,data[2], data[3]);
	}
}

// from iw source code, no idea what's going on here
void print_rsn_ie(const char *defcipher, const char *defauth,
	uint8_t len, const uint8_t *data, const char* section_name) {

	__u16 count, capa;
	int i;
	int is_osen = 0;
	bool first = true;

	dataline(section_name);
	if (!is_osen) {
		__u16 version;
		version = data[0] + (data[1] << 8);
		printf("version:%d\n", version);
		data += 2;
		len -= 2;
	}

	if (len < 4) {
		dataline(section_name);
		printf("group cipher:%s\n", defcipher);
		dataline(section_name);
		printf("pairwise ciphers:%s\n", defcipher);
		return;
	}

	dataline(section_name);
	printf("group cipher:");
	print_cipher(data);
	printf("\n");

	data += 4;
	len -= 4;

	if (len < 2) {
		dataline(section_name);
		printf("pairwise ciphers:%s\n", defcipher);
		return;
	}

	count = data[0] | (data[1] << 8);
	if (2 + (count * 4) > len) {
		goto invalid;
	}

	dataline(section_name);
	printf("pairwise ciphers:");
	for (i = 0; i < count; i++) {
		if (i > 0) printf(",");
		print_cipher(data + 2 + (i * 4));
	}
	printf("\n");

	data += 2 + (count * 4);
	len -= 2 + (count * 4);

	if (len < 2) {
		dataline(section_name);
		printf("authentication suites:%s\n", defauth);
		return;
	}

	count = data[0] | (data[1] << 8);
	if (2 + (count * 4) > len) {
		goto invalid;
	}

	dataline(section_name);
	printf("authentication suites:");
	for (i = 0; i < count; i++) {
		if (i > 0) printf(",");
		print_auth(data + 2 + (i * 4));
	}
	printf("\n");

	data += 2 + (count * 4);
	len -= 2 + (count * 4);

	if (len >= 2) {
		capa = data[0] | (data[1] << 8);
		dataline(section_name);
		printf("capabilities:");
		if (capa & 0x0001)
			{sep_if_not_first(&first); printf("PreAuth");}
		if (capa & 0x0002)
			{sep_if_not_first(&first); printf("NoPairwise");}
		switch ((capa & 0x000c) >> 2) {
		case 0:
			{sep_if_not_first(&first); printf("1-PTKSA-RC");
			break;}
		case 1:
			{sep_if_not_first(&first); printf("2-PTKSA-RC");
			break;}
		case 2:
			{sep_if_not_first(&first); printf("4-PTKSA-RC");
			break;}
		case 3:
			{sep_if_not_first(&first); printf("16-PTKSA-RC");
			break;}
		}
		switch ((capa & 0x0030) >> 4) {
		case 0:
			{sep_if_not_first(&first); printf("1-GTKSA-RC");
			break;}
		case 1:
			{sep_if_not_first(&first); printf("2-GTKSA-RC");
			break;}
		case 2:
			{sep_if_not_first(&first); printf("4-GTKSA-RC");
			break;}
		case 3:
			{sep_if_not_first(&first); printf("16-GTKSA-RC");
			break;}
		}
		if (capa & 0x0040)
			{sep_if_not_first(&first); printf("MFP-required");}
		if (capa & 0x0080)
			{sep_if_not_first(&first); printf("MFP-capable");}
		if (capa & 0x0200)
			{sep_if_not_first(&first); printf("Peerkey-enabled");}
		if (capa & 0x0400)
			{sep_if_not_first(&first); printf("SPP-AMSDU-capable");}
		if (capa & 0x0800)
			{sep_if_not_first(&first); printf("SPP-AMSDU-required");}
		if (capa & 0x2000)
			{sep_if_not_first(&first); printf("Extended-Key-ID");}
		{sep_if_not_first(&first); printf("(0x%.4x)", capa);}
		data += 2;
		len -= 2;
		printf("\n");
	}

	if (len >= 2) {
		int pmkid_count = data[0] | (data[1] << 8);

		if (len >= 2 + 16 * pmkid_count) {
			dataline(section_name);
			printf("PMKID count:%d\n", pmkid_count);
			/* not printing PMKID values */
			data += 2 + 16 * pmkid_count;
			len -= 2 + 16 * pmkid_count;
		} else {
			goto invalid;
		}
	}

	if (len >= 4) {
		dataline(section_name);
		printf("group mgmt cipher suite:");
		print_cipher(data);
		data += 4;
		len -= 4;
		printf("\n");
	}

invalid:
	if (len != 0) {
		dataline(section_name);
		printf("bogus tail data:%d", len);
		while (len) {
			printf(" %.2x", *data);
			data++;
			len--;
		}
		printf("\n");
	}

}

// from iw source code
void print_rsn(const uint8_t type, uint8_t len, const uint8_t *data,
	struct print_ies_data *ie_buffer, const char* section_name) {
	print_rsn_ie("CCMP", "IEEE 802.1X", len, data, section_name);
}

static void print_wifi_wpa(const uint8_t type, uint8_t len, const uint8_t *data,
	struct print_ies_data *ie_buffer, const char* section_name) {
	print_rsn_ie("TKIP", "IEEE 802.1X", len, data, section_name);
}

// this struct is used to create a handler for specific magic values of
// IE (information element) in the wifi probe or beacon responses. From what
// I gather, each IE requires a bit different type of parsing, and what I do
// here is just directly copied from how iw does it. There's tons of magic
// values in the code, and I couldn't figure out where they are defined.
struct ie_print {
	const char* name;
	void (*print)(const uint8_t type, uint8_t len, const uint8_t *data,
		struct print_ies_data *ie_buffer, const char* section_name);
	uint8_t minlen;
	uint8_t maxlen;
};

// This array size needs to be adjusted if magic values go beyond it. The array
// contains empty elements for each type of IE that is not handled and is only 
// modified at those points where we have an IE handler. See how it is done in
// the beginning of main()
const int MAX_IE_MAGIC = 112;
static struct ie_print ieprinters[MAX_IE_MAGIC];

const int MAX_VENDOR_MAGIC = 112;
static struct ie_print wifiprinters[MAX_VENDOR_MAGIC];

// print a single IE parsed from a probe request or beacon response
static void print_ie(const struct ie_print *p, const uint8_t type, uint8_t len, 
	const uint8_t *data, struct print_ies_data *ie_buffer) {

	// If no printer function is defined for type of IE
	if (p->print == NULL) {
		return;
	}

	if (len < p->minlen || len > p->maxlen) {
		if (len > 1) {
			printf(",invalid %d bytes:", len);
		} else if (len) {
			printf(",invalid:1 byte %.02x>\n", data[0]);
		}  else {
			printf(",invalid:no data");
		}
		return;
	}

	p->print(type, len, data, ie_buffer, p->name);
}

static void print_vendor(unsigned char len, unsigned char *data)
{
	if (len < 3) {
		return;
	}

	if (len >= 4 && memcmp(data, ms_oui, 3) == 0) {
		if (data[3] < ARRAY_SIZE(wifiprinters) &&
		    wifiprinters[data[3]].name) {
			print_ie(&wifiprinters[data[3]], data[3], len - 4, data + 4, NULL);
			return;
		}

		return;
	}
}

// Go through all information elements and print them if a printer for them is defined
void print_ies(unsigned char *ie, int ielen) {
	struct print_ies_data ie_buffer = {
		.ie = ie,
		.ielen = ielen };

	if (ie == NULL || ielen < 0) {
		return;
	}

	while (ielen >= 2 && ielen - 2 >= ie[1]) {
		if (ie[0] < ARRAY_SIZE(ieprinters) && ieprinters[ie[0]].name) {
			print_ie(&ieprinters[ie[0]], ie[0], ie[1], ie + 2, &ie_buffer);
		} else if (ie[0] == 221) {
			print_vendor(ie[1], ie + 2);
		}

		ielen -= ie[1] + 2;
		ie += ie[1] + 2;
	}
}

// Called by the kernel when the scan is done or has been aborted
int scan_finished_cb(struct nl_msg* msg, void* arg) {

	struct genlmsghdr* gnlh = (genlmsghdr*)nlmsg_data(nlmsg_hdr(msg));
	struct init_scan_results* results = (init_scan_results*)arg;

	if (gnlh->cmd == NL80211_CMD_SCAN_ABORTED) {
		results->done = 1;
		results->aborted = 1;
	} else if (gnlh->cmd == NL80211_CMD_NEW_SCAN_RESULTS) {
		results->done = 1;
		results->aborted = 0;
	}
	// else probably an uninteresting multicast message.

	return NL_SKIP;
}

// Called by the kernel with a dump of the successful scan's data. Called for each SSID.
int receive_scan_result(struct nl_msg *msg, void *arg) {

	struct genlmsghdr* gnlh = (genlmsghdr*)nlmsg_data(nlmsg_hdr(msg));

	// Container for netlink attribute indices, each pointing to different parts of the
	// netlink message stream. These can be used to then parse further attributes from
	// the stream. Go read netlink documentation and see if you have more luck
	// understanding how the messaging works.
	struct nlattr* tb[NL80211_ATTR_MAX + 1];

	// container for parsing the access point's basic service set information (BSS)
	struct nlattr* bss[NL80211_BSS_MAX + 1];

	// container specifying the types and lengths of data to be parsed from the netlink
	// message, I think. The whole message parsing side of netlink is confusing.
	struct nla_policy bss_policy[NL80211_BSS_MAX + 1];

	memset(bss_policy, 0, sizeof(bss_policy));
	memset(bss, 0, sizeof(bss));
	memset(tb, 0, sizeof(tb));

	bss_policy[NL80211_BSS_TSF] = { .type = NLA_U64 };
	bss_policy[NL80211_BSS_FREQUENCY] = { .type = NLA_U32 },
	bss_policy[NL80211_BSS_BSSID] = { };
	bss_policy[NL80211_BSS_BEACON_INTERVAL] = { .type = NLA_U16 };
	bss_policy[NL80211_BSS_CAPABILITY] = { .type = NLA_U16 };
	bss_policy[NL80211_BSS_INFORMATION_ELEMENTS] = { };
	bss_policy[NL80211_BSS_SIGNAL_MBM] = { .type = NLA_U32 };
	bss_policy[NL80211_BSS_SIGNAL_UNSPEC] = { .type = NLA_U8 };
	bss_policy[NL80211_BSS_STATUS] = { .type = NLA_U32 };
	bss_policy[NL80211_BSS_SEEN_MS_AGO] = { .type = NLA_U32 };
	bss_policy[NL80211_BSS_BEACON_IES] = { };
	bool is_dmg = false;

	int err = nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);
	if (err < 0) {
		printf("error creating attribute indices from scan message: %d, %s\n", err, nl_geterror(err));
		return NL_SKIP;
	}

	if (!tb[NL80211_ATTR_BSS]) {
		printf("bss info missing\n");
		return NL_SKIP;
	}

	// BSS information is a nested attribute, so a second parse call is needed
	err = nla_parse_nested(bss, NL80211_BSS_MAX, tb[NL80211_ATTR_BSS], bss_policy);
	if (err < 0) {
		printf("failed to parse nested attributes: %d, %s\n", err, nl_geterror(err));
		return NL_SKIP;
	}

	// If BSSID or IE is missing, we can't parse anything beyond this point
	if (!bss[NL80211_BSS_BSSID] || !bss[NL80211_BSS_INFORMATION_ELEMENTS]) {
		return NL_SKIP;
	}

	memset(current_mac, '\0', sizeof(current_mac));
	mac_addr_n2a(current_mac, (unsigned char*)nla_data(bss[NL80211_BSS_BSSID]));

	printf("%s%s\n", DISCOVER_STR, current_mac);

	if (bss[NL80211_BSS_SIGNAL_MBM]) {
		dataline();
		printf("signal strength:%d mBm\n", nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]));
	} else if (bss[NL80211_BSS_SIGNAL_UNSPEC]) {
		dataline();
		printf("signal strength:%d units\n", nla_get_u8(bss[NL80211_BSS_SIGNAL_UNSPEC]));
	}

	if (bss[NL80211_BSS_FREQUENCY]) {
		int freq = nla_get_u32(bss[NL80211_BSS_FREQUENCY]);

		dataline();
		int freq_offset = bss[NL80211_BSS_FREQUENCY_OFFSET] ? nla_get_u32(bss[NL80211_BSS_FREQUENCY_OFFSET]) : 0;
		if (freq_offset > 0)
			printf("frequency:%d.%d MHz\n", freq, freq_offset);
		else
			printf("frequency:%d MHz\n", freq);

		if (freq > 45000)
			is_dmg = true;
	}

	if (bss[NL80211_BSS_CAPABILITY]) {
		__u16 capa = nla_get_u16(bss[NL80211_BSS_CAPABILITY]);
		bool first = true;
		dataline();
		printf("capabilities:");
		if (is_dmg)
			print_capa_dmg(capa, &first);
		else
			print_capa_non_dmg(capa, &first);
		
		sep_if_not_first(&first);
		printf("(0x%.4x)\n", capa);
	}

	// Information element parsing is based entirely on iw source code. There's a ton of undocumented
	// magic values going around, and I didn't really get an understanding how IE is bundled into
	// scan responses, but it seems to be binary data of custom structure.	
	if (bss[NL80211_BSS_INFORMATION_ELEMENTS]) {

		struct nlattr* ies = bss[NL80211_BSS_INFORMATION_ELEMENTS];
		struct nlattr* bcnies = bss[NL80211_BSS_BEACON_IES];

		if (bss[NL80211_BSS_PRESP_DATA] || (bcnies && (nla_len(ies) != nla_len(bcnies) ||
			memcmp(nla_data(ies), nla_data(bcnies), nla_len(ies))))) {
		}

		print_ies((unsigned char*)nla_data(ies), nla_len(ies));
	}

	// There can be both beacon responses and probe requests in the same scan result, and they
	// can contain the same data. This can result in duplicates being printed.
	if (bss[NL80211_BSS_BEACON_IES]) {
		print_ies((unsigned char*)nla_data(bss[NL80211_BSS_BEACON_IES]), nla_len(bss[NL80211_BSS_BEACON_IES]));
	}

	printf("\n");

	return NL_SKIP;
}

int do_scan_trigger(struct nl_sock* socket, int if_index, int family_id) {

	// Starts the scan and waits for it to finish.
	// Does not return until the scan is done or has been aborted.

	struct init_scan_results results = { .done = 0, .aborted = 0 };
	struct nl_msg* msg = NULL;
	struct nl_msg* ssids_to_scan = NULL;
	struct nl_cb* cb = NULL;
	int err;
	int ret;
	int mcid = -1;

	std::shared_ptr<void> defer(nullptr, [&](...){
		if (ssids_to_scan != NULL) {
			nlmsg_free(ssids_to_scan);
		}

		if (msg != NULL) {
			nlmsg_free(msg);
		}

		if (cb != NULL) {
			nl_cb_put(cb);
		}

		if (mcid >= 0) {
			nl_socket_drop_membership(socket, mcid);
		}
	});

	mcid = genl_ctrl_resolve_grp(socket, "nl80211", "scan");

	if (mcid < 0) {
		printf("error resolving netlink group name to identifier: %d, %s\n",
			mcid, nl_geterror(err));
		return 1;
	}

	// join the netlink socket into the scan group resolved above
	err = nl_socket_add_membership(socket, mcid);
	if (err < 0) {
		printf("error joining scan group: %d, %s\n", err, nl_geterror(err));
		return 1;
	}

	// Allocate netlink messages with the default size
	msg = nlmsg_alloc();
	ssids_to_scan = nlmsg_alloc();

	if (msg == NULL || ssids_to_scan == NULL) {
		printf("Failed allocating netlink message\n");
		return 1;
	}

	// allocate a callback handle with default quiet callback type
	cb = nl_cb_alloc(NL_CB_DEFAULT);

	if (!cb) {
		printf("Failed allocating callback\n");
		return 1;
	}

	// Setup the messages and callback handler.

	// Construct message header
	// I think this function returns something relevant only if the user_header parameter
	// is specified as non-zero? I have no idea.
	genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, family_id, 0, 0, NL80211_CMD_TRIGGER_SCAN, 0);

	// Add message attribute specifying which interface to use.
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, if_index);

	// Scan all SSIDs
	// TODO: what are these values?
	nla_put(ssids_to_scan, 1, 0, "");

	// Add message attribute specifiying which SSIDs to scan for
	nla_put_nested(msg, NL80211_ATTR_SCAN_SSIDS, ssids_to_scan);

	// Copied to msg above, no longer need this
	nlmsg_free(ssids_to_scan);
	ssids_to_scan = NULL;

	// Add callbacks - apparently the same callback handle is used for all of them?
	ret = nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
	if (ret < 0) {
		printf("Failed setting NL_CB_CUSTOM callback: %d, %s\n", ret, nl_geterror(ret));;
		return 1;
	}

	ret = nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, scan_finished_cb, &results);
	if (ret < 0) {
		printf("Failed setting NL_CB_VALID callback: %d, %s\n", ret, nl_geterror(ret));
		return 1;
	}

	ret = nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	if (ret < 0) {
		printf("Failed setting NL_CB_FINISH callback: %d, %s\n", ret, nl_geterror(ret));
		return 1;
	}

	ret = nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);
	if (ret < 0) {
		printf("Failed setting NL_CB_ACK callback: %d, %s\n", ret, nl_geterror(ret));
		return 1;
	}

	// No sequence checking for multicast messages
	ret = nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, no_seq_check, NULL);
	if (ret < 0) {
		printf("Failed setting NL_CB_SEQ_CHECK callback: %d, %s\n", ret, nl_geterror(ret));
		return 1;
	}

	// reset error flag
	err = 1;

	// Send NL80211_CMD_TRIGGER_SCAN to start the scan.
	// The kernel may reply with NL80211_CMD_NEW_SCAN_RESULTS on success or
	// NL80211_CMD_SCAN_ABORTED if another scan was started by another process.

	int written = nl_send_auto(socket, msg);
	if (written < 0) {
		printf("error in nl_send_auto: %d, %s\n", written, nl_geterror(written));
		return 1;
	}

	printf("nl_send_auto wrote %d bytes\n", written);
	printf("Waiting for scan to complete\n");

	// wait for NL_CB_ACK|error_handler
	while (err > 0) {
		ret = nl_recvmsgs(socket, cb);
		if (ret < 0) {
			fprintf(stderr, "nl_recvmsgs returned error: %d, %s\n", ret, nl_geterror(ret));
			if (err >= 0)
				return 1;
			else
			  	break;
		}
	}

	if (err < 0) {
		fprintf(stderr, "error flag set during message transmission: %d, %s\n", err, strerror(-err));
		return err;
	}

	while (results.done != 1) {
		// Now wait until the scan is done or aborted
		nl_recvmsgs(socket, cb);
	}

	if (results.aborted == 1) {
		printf("scan was aborted\n");
		return 1;
	}

	printf("Scan is done\n");
	return 0;
}

int main(int argc, char** argv) {

	if (argc < 2) {
		printf("usage: programname wifi_adapter_name\nie: ./programname wlp2s0.\n");
		return 1;
	}

	// Specify information element parsers. I don't know where one finds what these
	// magic values are supposed to be. They are copied from iw source.
	memset(ieprinters, 0, sizeof(ieprinters));
	ieprinters[0] = { "SSID", print_ssid, 0, 32 };
	ieprinters[48] = { "RSN", print_rsn, 2, 255 };

	memset(wifiprinters, 0, sizeof(wifiprinters));
	wifiprinters[1] = { "WPA", print_wifi_wpa, 2, 255 };
	//wifiprinters[2] = { "WMM", print_wifi_wmm, 1, 255, };
	wifiprinters[4] = { "WPS", print_wifi_wps, 0, 255 };

	memset(current_mac, '\0', sizeof(current_mac));

	const char* ifname = argv[1];
	printf("Using interface: %s\n", ifname);

	int if_index = if_nametoindex(ifname);
	if (if_index == 0) {
		printf("error matching interface %s into a real interface: %d, %s\n",
			ifname, errno, strerror(errno));
		return 1;
	}

	// Allocate a netlink socket
	struct nl_sock* nlsocket = nl_socket_alloc();
	if (nlsocket == NULL) {
		printf("Failed allocating nl socket\n");
		return 1;
	}

	struct nl_msg* msg = NULL;

	// cleanup when falling out of scope
	std::shared_ptr<void> defer(nullptr, [&](...){
		if (nlsocket) {
			nl_socket_free(nlsocket);
			nlsocket = NULL;
		}

		if (msg) {
			nlmsg_free(msg);
		}
	});

	// Connect the allocated socket to libnl
	int err = genl_connect(nlsocket);
	if (err < 0) {
		printf("Error connecting nl socket: %d, %s\n", err, nl_geterror(err));
		return 1;
	}

	// Match the nl80211 netlink family name to its identifier
	int family_id = genl_ctrl_resolve(nlsocket, "nl80211");
	if (family_id  < 0) {
		printf("error finding identifier for nl80211 family name: %d, %s\n",
			family_id, nl_geterror(family_id));
		return 1;
	}

	// Issue NL80211_CMD_TRIGGER_SCAN to the kernel and wait for it to finish
	err = do_scan_trigger(nlsocket, if_index, family_id);

	if (err != 0) {
		printf("do_scan_trigger() failed with %d\n", err);
		return err > 0 ? err : -err;
	}

	// get info for all SSIDs detected

	msg = nlmsg_alloc();

	// Setup which command to run
	genlmsg_put(msg, 0, 0, family_id, 0, NLM_F_DUMP, NL80211_CMD_GET_SCAN, 0);

	// Add message attribute specifying which interface to use
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, if_index);

	// Add callback for getting data
	nl_socket_modify_cb(nlsocket, NL_CB_VALID, NL_CB_CUSTOM, receive_scan_result, NULL);

	// Send the message
	int ret = nl_send_auto(nlsocket, msg);
	if (ret < 0) {
		printf("nl_send_auto() failed with: %d, %s\n", ret, nl_geterror(ret));
		return 1;
	}

	// wait for the message to go through
	ret = nl_recvmsgs_default(nlsocket);

	// TODO: handle invalid number of bytes written
	if (ret < 0) {
		printf("ERROR: nl_recvmsgs_default() failed with %d, %s\n", ret, nl_geterror(ret));
		return 1;
	}

	return 0;
}
