# libnl based wifi access point scanner

This repo was forked from "Delicode/libnl-ap-scanner" because its output has changed and is not backwards compatible.

*Updates:*

Aug 5, 2023:
- print BSS capability

Aug 4, 2023:
- signal strength bug fixed (u8 -> u32)
- enum values now comma separated because the values can contains spaces (e.g.: "IEEE 802.1X")
- bogus separators and whitespaces removed
- section name added
- WPA and WPS information added
- Bitbake recipe for OpenEmbedded (Bitbake 2.0 [kirkstone] or higher required)

### Usage
JS regexps for parsing (**use** case-insensitive matching).

for DISCOVERED lines:
```
^AP_DISCOVERED,([A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2})$
```
for DATA lines:
```
^AP_DATA,([A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}),([\w\d]+),([\-\w\d\s]+)(?::(.*))?$
```
for Signal strength value:
```
:(?:(?:(-?\d+) (mBm))|(?:(\d{1,3}) (units)))$
```

#### Using the signal strength value
```
dBm = mBm / 100
units = 0..100    (signal quality in percent)
```

#### Output

The goal is still to have a stable output. The output of the program is approximately as follows:
```
$ make && sudo ./ap-scanner wlp2s0
make: Nothing to be done for 'all'.
Using interface: wlp2s0
nl_send_auto wrote 36 bytes
Waiting for scan to complete
Scan is done
AP_DISCOVERED,2c:56:dc:5c:8e:85
AP_DATA,2c:56:dc:5c:8e:85,BSS,signal strength:-4700 mBm
AP_DATA,2c:56:dc:5c:8e:85,BSS,frequency:2437 MHz
AP_DATA,2c:56:dc:5c:8e:85,BSS,capabilities:ESS,Privacy,SpectrumMgmt,RadioMeasure,(0x1111)
AP_DATA,2c:56:dc:5c:8e:85,BSS,ssid:Domain1
AP_DATA,2c:56:dc:5c:8e:85,RSN,version:1
AP_DATA,2c:56:dc:5c:8e:85,RSN,group cipher:CCMP
AP_DATA,2c:56:dc:5c:8e:85,RSN,pairwise ciphers:CCMP
AP_DATA,2c:56:dc:5c:8e:85,RSN,authentication suites:IEEE 802.1X
AP_DATA,2c:56:dc:5c:8e:85,RSN,capabilities:1-PTKSA-RC,1-GTKSA-RC,(0x0000)

AP_DISCOVERED,37:c5:9a:31:fa:8c
AP_DATA,37:c5:9a:31:fa:8c,BSS,signal strength:-2600 mBm
AP_DATA,37:c5:9a:31:fa:8c,BSS,frequency:5180 MHz
AP_DATA,37:c5:9a:31:fa:8c,BSS,capabilities:ESS,Privacy,ShortPreamble,(0x0031)
AP_DATA,37:c5:9a:31:fa:8c,BSS,ssid:Iot-Test_5G
AP_DATA,37:c5:9a:31:fa:8c,RSN,version:1
AP_DATA,37:c5:9a:31:fa:8c,RSN,group cipher:CCMP
AP_DATA,37:c5:9a:31:fa:8c,RSN,pairwise ciphers:CCMP
AP_DATA,37:c5:9a:31:fa:8c,RSN,authentication suites:SAE
AP_DATA,37:c5:9a:31:fa:8c,RSN,capabilities:16-PTKSA-RC,1-GTKSA-RC,MFP-required,MFP-capable,(0x00cc)

AP_DISCOVERED,a8:56:28:af:0a:0f
AP_DATA,a8:56:28:af:0a:0f,BSS,signal strength:-2600 mBm
AP_DATA,a8:56:28:af:0a:0f,BSS,frequency:2462 MHz
AP_DATA,a8:56:28:af:0a:0f,BSS,capabilities:ESS,Privacy,SpectrumMgmt,ShortSlotTime,RadioMeasure,(0x1511)
AP_DATA,a8:56:28:af:0a:0f,BSS,ssid:Iot-Test
AP_DATA,a8:56:28:af:0a:0f,RSN,version:1
AP_DATA,a8:56:28:af:0a:0f,RSN,group cipher:TKIP
AP_DATA,a8:56:28:af:0a:0f,RSN,pairwise ciphers:CCMP,TKIP
AP_DATA,a8:56:28:af:0a:0f,RSN,authentication suites:PSK
AP_DATA,a8:56:28:af:0a:0f,RSN,capabilities:16-PTKSA-RC,1-GTKSA-RC,(0x000c)
AP_DATA,a8:56:28:af:0a:0f,WPS,version:1.0
AP_DATA,a8:56:28:af:0a:0f,WPS,wi-fi protected setup state:2 (Configured)
AP_DATA,a8:56:28:af:0a:0f,WPS,response type:3 (AP)
AP_DATA,a8:56:28:af:0a:0f,WPS,uuid:5c1f07ad-9d5a-8fe6-53ca-27fb6e1db5c2
AP_DATA,a8:56:28:af:0a:0f,WPS,manufacturer:TP-Link
AP_DATA,a8:56:28:af:0a:0f,WPS,model:Archer AX10
AP_DATA,a8:56:28:af:0a:0f,WPS,model Number:123456
AP_DATA,a8:56:28:af:0a:0f,WPS,serial number:1234
AP_DATA,a8:56:28:af:0a:0f,WPS,primary device type:6-0050f204-1
AP_DATA,a8:56:28:af:0a:0f,WPS,device name:Archer AX10
AP_DATA,a8:56:28:af:0a:0f,WPS,config methods:Display
AP_DATA,a8:56:28:af:0a:0f,WPS,rf bands:0x3
AP_DATA,a8:56:28:af:0a:0f,WPS,version2:2.0
AP_DATA,a8:56:28:af:0a:0f,WPA,version:1
AP_DATA,a8:56:28:af:0a:0f,WPA,group cipher:TKIP
AP_DATA,a8:56:28:af:0a:0f,WPA,pairwise ciphers:CCMP,TKIP
AP_DATA,a8:56:28:af:0a:0f,WPA,authentication suites:PSK
```

### Dependencies

* libnl. On Debian, install: `libnl-3-dev libnl-genl-3-dev`

### Other developers
The is code is applied from:

* libnl sources: [https://www.infradead.org/~tgr/libnl/](https://www.infradead.org/~tgr/libnl/)
* example code from Python libnl port: [https://github.com/Robpol86/libnl/blob/master/example_c/scan_access_points.c](https://github.com/Robpol86/libnl/blob/master/example_c/scan_access_points.c)
* iw(8) source code: [https://git.kernel.org/pub/scm/linux/kernel/git/jberg/iw.git](https://git.kernel.org/pub/scm/linux/kernel/git/jberg/iw.git)

This changed version of the example program `scan_access_points.c` addresses several errors that were not handled, scans for more information, rearranges the code in a cleaner format and improves on the documentation. In addition, several memory leaks with allocated libnl resources are handled.
