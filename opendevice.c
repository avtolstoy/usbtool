/* Name: opendevice.c
 * Project: usbtool
 * Author: Christian Starkjohann, Andrey Tolstoy
 * Creation Date: 2016-05-26
 * Tabsize: 4
 * Copyright: (c) 2008 by OBJECTIVE DEVELOPMENT Software GmbH
 *            (c) 2016 by Andrey Tolstoy, LLC Enistek
 * License: GNU GPL v2 (see License.txt), GNU GPL v3 or proprietary (CommercialLicense.txt)
 */

/*
General Description:
The functions in this module can be used to find and open a device based on
libusb-1.0
*/

#include <stdio.h>
#include "opendevice.h"

extern libusb_context* usbCtx;

/* ------------------------------------------------------------------------- */

#define MATCH_SUCCESS			1
#define MATCH_FAILED			0
#define MATCH_ABORT				-1

/* private interface: match text and p, return MATCH_SUCCESS, MATCH_FAILED, or MATCH_ABORT. */
static int  _shellStyleMatch(char *text, char *p)
{
int last, matched, reverse;

    for(; *p; text++, p++){
        if(*text == 0 && *p != '*')
            return MATCH_ABORT;
        switch(*p){
        case '\\':
            /* Literal match with following character. */
            p++;
            /* FALLTHROUGH */
        default:
            if(*text != *p)
                return MATCH_FAILED;
            continue;
        case '?':
            /* Match anything. */
            continue;
        case '*':
            while(*++p == '*')
                /* Consecutive stars act just like one. */
                continue;
            if(*p == 0)
                /* Trailing star matches everything. */
                return MATCH_SUCCESS;
            while(*text)
                if((matched = _shellStyleMatch(text++, p)) != MATCH_FAILED)
                    return matched;
            return MATCH_ABORT;
        case '[':
            reverse = p[1] == '^';
            if(reverse) /* Inverted character class. */
                p++;
            matched = MATCH_FAILED;
            if(p[1] == ']' || p[1] == '-')
                if(*++p == *text)
                    matched = MATCH_SUCCESS;
            for(last = *p; *++p && *p != ']'; last = *p)
                if (*p == '-' && p[1] != ']' ? *text <= *++p && *text >= last : *text == *p)
                    matched = MATCH_SUCCESS;
            if(matched == reverse)
                return MATCH_FAILED;
            continue;
        }
    }
    return *text == 0;
}

/* public interface for shell style matching: returns 0 if fails, 1 if matches */
static int shellStyleMatch(char *text, char *pattern)
{
    if(pattern == NULL) /* NULL pattern is synonymous to "*" */
        return 1;
    return _shellStyleMatch(text, pattern) == MATCH_SUCCESS;
}

/* ------------------------------------------------------------------------- */

int usbGetStringAscii(libusb_device_handle *dev, int index, char *buf, int buflen)
{
    char    buffer[256];
    int     rval, i;

    if((rval = libusb_get_string_descriptor_ascii(dev, index & 0xff, (unsigned char*)buf, buflen)) >= 0)
        return rval;

    return -1;

    // if(buffer[1] != USB_DT_STRING){
    //     *buf = 0;
    //     return 0;
    // }
    // if((unsigned char)buffer[0] < rval)
    //     rval = (unsigned char)buffer[0];
    // rval /= 2;
    // /* lossy conversion to ISO Latin1: */
    // for(i=1;i<rval;i++){
    //     if(i > buflen)              /* destination buffer overflow */
    //         break;
    //     buf[i-1] = buffer[2 * i];
    //     if(buffer[2 * i + 1] != 0)  /* outside of ISO Latin1 range */
    //         buf[i-1] = '?';
    // }
    // buf[i-1] = 0;
    // return i-1;
}

/* ------------------------------------------------------------------------- */

int usbOpenDevice(libusb_device_handle **device, int vendorID, char *vendorNamePattern, int productID, char *productNamePattern, char *serialNamePattern, FILE *printMatchingDevicesFp, FILE *warningsFp)
{
    libusb_device_handle      *handle = NULL;
    int                 errorCode = USBOPEN_ERR_NOTFOUND;

    libusb_device **devs;

    int cnt = libusb_get_device_list(usbCtx, &devs);
    for (int i = 0; i < cnt; i++) {
        libusb_device* dev = devs[i];
        struct libusb_device_descriptor desc;
        int r = libusb_get_device_descriptor(dev, &desc);
        if (r < 0)
            continue;

        if((vendorID == 0 || desc.idVendor == vendorID) 
           && (productID == 0 || desc.idProduct == productID)) {
            char    vendor[256], product[256], serial[256];
            int len = 0;
            r = libusb_open(dev, &handle);
            if (r < 0) {
                errorCode = USBOPEN_ERR_ACCESS;
                if(warningsFp != NULL)
                    fprintf(warningsFp, "Warning: cannot open VID=0x%04x PID=0x%04x: %s\n", desc.idVendor, desc.idProduct, libusb_error_name(r));
                continue;
            }

            if (desc.idVendor == vendorID && desc.idProduct == productID)
                break;

            len = vendor[0] = 0;
            if (desc.iManufacturer > 0) {
                len = usbGetStringAscii(handle, desc.iManufacturer, vendor, sizeof(vendor));
            }

            if (len < 0) {
                errorCode = USBOPEN_ERR_ACCESS;
                if(warningsFp != NULL)
                    fprintf(warningsFp, "Warning: cannot query manufacturer for VID=0x%04x PID=0x%04x: %s\n", desc.idVendor, desc.idProduct, libusb_error_name(len));
            } else {
                errorCode = USBOPEN_ERR_NOTFOUND;
                /* printf("seen device from vendor ->%s<-\n", vendor); */
                if(shellStyleMatch(vendor, vendorNamePattern)){
                    len = product[0] = 0;
                    if(desc.iProduct > 0){
                        len = usbGetStringAscii(handle, desc.iProduct, product, sizeof(product));
                    }
                    if(len < 0){
                        errorCode = USBOPEN_ERR_ACCESS;
                        if(warningsFp != NULL)
                            fprintf(warningsFp, "Warning: cannot query product for VID=0x%04x PID=0x%04x: %s\n", desc.idVendor, desc.idProduct, libusb_error_name(len));
                    }else{
                        errorCode = USBOPEN_ERR_NOTFOUND;
                        /* printf("seen product ->%s<-\n", product); */
                        if(shellStyleMatch(product, productNamePattern)){
                            len = serial[0] = 0;
                            if(desc.iSerialNumber > 0){
                                len = usbGetStringAscii(handle, desc.iSerialNumber, serial, sizeof(serial));
                            }
                            if(len < 0){
                                errorCode = USBOPEN_ERR_ACCESS;
                                if(warningsFp != NULL)
                                    fprintf(warningsFp, "Warning: cannot query serial for VID=0x%04x PID=0x%04x: %s\n", desc.idVendor, desc.idProduct, libusb_error_name(len));
                            }
                            if(shellStyleMatch(serial, serialNamePattern)){
                                if(printMatchingDevicesFp != NULL){
                                    if(serial[0] == 0){
                                        fprintf(printMatchingDevicesFp, "VID=0x%04x PID=0x%04x vendor=\"%s\" product=\"%s\"\n", desc.idVendor, desc.idProduct, vendor, product);
                                    }else{
                                        fprintf(printMatchingDevicesFp, "VID=0x%04x PID=0x%04x vendor=\"%s\" product=\"%s\" serial=\"%s\"\n", desc.idVendor, desc.idProduct, vendor, product, serial);
                                    }
                                }else{
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            libusb_close(handle);
            handle = NULL;
        }

    }
    libusb_free_device_list(devs, 1);

    if(handle != NULL){
        errorCode = 0;
        *device = handle;
    }
    if(printMatchingDevicesFp != NULL)  /* never return an error for listing only */
        errorCode = 0;
    return errorCode;
}

/* ------------------------------------------------------------------------- */
