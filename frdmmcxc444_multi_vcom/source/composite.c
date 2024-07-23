/*
 * Copyright 2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"

#include "usb_device_cdc_acm.h"
#include "usb_device_ch9.h"
#include "usb_device_descriptor.h"

#include "fsl_device_registers.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"

#include <stdio.h>
#include <stdlib.h>
#include "composite.h"
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
#include "fsl_sysmpu.h"
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

#if ((defined FSL_FEATURE_SOC_USBPHY_COUNT) && (FSL_FEATURE_SOC_USBPHY_COUNT > 0U))
#include "usb_phy.h"
#endif

#include "fsl_common.h"
#include "pin_mux.h"
/*******************************************************************************
* Variables
******************************************************************************/
/* Composite device structure. */
usb_device_composite_struct_t g_composite;

extern uint8_t g_UsbDeviceConfigurationDescriptor[];
extern uint8_t g_CdcConfigurationDescriptorTemplate[];
extern uint8_t g_CdcDescriptorTemplate[];

/* CDC VCOM DIC Bulk IN endpoint number*/
extern uint8_t g_CdcVcomDicBulkInEndpoint[USB_DEVICE_CONFIG_CDC_ACM];

/* CDC VCOM DIC Bulk OUT endpoint number*/
extern uint8_t g_CdcVcomDicBulkOutEndpoint[USB_DEVICE_CONFIG_CDC_ACM];
extern uint8_t g_CdcVcomDicInterfaceIndex[USB_DEVICE_CONFIG_CDC_ACM];
extern uint8_t g_CdcVcomCicInterfaceIndex[USB_DEVICE_CONFIG_CDC_ACM];
extern uint8_t g_CdcVcomCicInterruptInEndpoint[USB_DEVICE_CONFIG_CDC_ACM];

/*******************************************************************************
* Definitions
******************************************************************************/
/*******************************************************************************
* Prototypes
******************************************************************************/
void BOARD_InitHardware(void);
void USB_DeviceClockInit(void);
void USB_DeviceIsrEnable(void);
#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void *deviceHandle);
#endif
/*******************************************************************************
* Code
******************************************************************************/

void USB0_IRQHandler(void)
{
    USB_DeviceKhciIsrFunction(g_composite.deviceHandle);
}
void USB_DeviceClockInit(void)
{
    SystemCoreClockUpdate();
    CLOCK_EnableUsbfs0Clock(kCLOCK_UsbSrcIrc48M, 48000000U);
}
void USB_DeviceIsrEnable(void)
{
    uint8_t irqNumber;

    uint8_t usbDeviceKhciIrq[] = USB_IRQS;
    irqNumber                  = usbDeviceKhciIrq[CONTROLLER_ID - kUSB_ControllerKhci0];

    /* Install isr, set priority, and enable IRQ. */
    NVIC_SetPriority((IRQn_Type)irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
    EnableIRQ((IRQn_Type)irqNumber);
}
#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void *deviceHandle)
{
    USB_DeviceKhciTaskFunction(deviceHandle);
}
#endif
/*!
 * @brief USB device callback function.
 *
 * This function handles the usb device specific requests.
 *
 * @param handle          The USB device handle.
 * @param event           The USB device event type.
 * @param param           The parameter of the device specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_Error;
    uint8_t *temp8 = (uint8_t *)param;

    switch (event)
    {
        case kUSB_DeviceEventBusReset:
        {
            USB_DeviceControlPipeInit(handle);
            g_composite.attach = 0;
            g_composite.currentConfiguration = 0;
#if (defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)) || \
    (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
            /* Get USB speed to configure the device, including max packet size and interval of the endpoints. */
            if (kStatus_USB_Success == USB_DeviceGetStatus(handle, kUSB_DeviceStatusSpeed, &g_composite.speed))
            {
                USB_DeviceSetSpeed(handle, g_composite.speed);
            }
#endif
        }
        break;
        case kUSB_DeviceEventSetConfiguration:
            if (0U == (*temp8))
            {
                g_composite.attach = 0U;
                g_composite.currentConfiguration = 0U;
            }
            else if (USB_COMPOSITE_CONFIGURE_INDEX == (*temp8))
            {
                g_composite.attach = 1;
                USB_DeviceCdcVcomSetConfigure(handle, *temp8);
                g_composite.currentConfiguration = *temp8;
                error = kStatus_USB_Success;
            }
            else
            {
                error = kStatus_USB_InvalidRequest;
            }
            break;
        default:
            break;
    }

    return error;
}

/*!
 * @brief Get the setup packet buffer.
 *
 * This function provides the buffer for setup packet.
 *
 * @param handle The USB device handle.
 * @param setupBuffer The pointer to the address of setup packet buffer.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetSetupBuffer(usb_device_handle handle, usb_setup_struct_t **setupBuffer)
{
    static uint32_t compositeSetup[2];
    if (NULL == setupBuffer)
    {
        return kStatus_USB_InvalidParameter;
    }
    *setupBuffer = (usb_setup_struct_t *)&compositeSetup;
    return kStatus_USB_Success;
}

/*!
 * @brief Get the vendor request data buffer.
 *
 * This function gets the data buffer for vendor request.
 *
 * @param handle The USB device handle.
 * @param setup The pointer to the setup packet.
 * @param length The pointer to the length of the data buffer.
 * @param buffer The pointer to the address of setup packet data buffer.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetVendorReceiveBuffer(usb_device_handle handle,
                                              usb_setup_struct_t *setup,
                                              uint32_t *length,
                                              uint8_t **buffer)
{
    return kStatus_USB_Error;
}

/*!
 * @brief CDC vendor specific callback function.
 *
 * This function handles the CDC vendor specific requests.
 *
 * @param handle The USB device handle.
 * @param setup The pointer to the setup packet.
 * @param length The pointer to the length of the data buffer.
 * @param buffer The pointer to the address of setup packet data buffer.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceProcessVendorRequest(usb_device_handle handle,
                                            usb_setup_struct_t *setup,
                                            uint32_t *length,
                                            uint8_t **buffer)
{
    return kStatus_USB_InvalidRequest;
}

/*!
 * @brief Configure remote wakeup feature.
 *
 * This function configures the remote wakeup feature.
 *
 * @param handle The USB device handle.
 * @param enable 1: enable, 0: disable.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceConfigureRemoteWakeup(usb_device_handle handle, uint8_t enable)
{
    return kStatus_USB_InvalidRequest;
}

/*!
 * @brief USB configure endpoint function.
 *
 * This function configure endpoint status.
 *
 * @param handle The USB device handle.
 * @param ep Endpoint address.
 * @param status A flag to indicate whether to stall the endpoint. 1: stall, 0: unstall.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceConfigureEndpointStatus(usb_device_handle handle, uint8_t ep, uint8_t status)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    error = USB_DeviceCdcVcomConfigureEndpointStatus(handle, ep, status);

    return error;
}

/*!
 * @brief Get the setup packet data buffer.
 *
 * This function gets the data buffer for setup packet.
 *
 * @param handle The USB device handle.
 * @param setup The pointer to the setup packet.
 * @param length The pointer to the length of the data buffer.
 * @param buffer The pointer to the address of setup packet data buffer.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetClassReceiveBuffer(usb_device_handle handle,
                                             usb_setup_struct_t *setup,
                                             uint32_t *length,
                                             uint8_t **buffer)
{
    static uint8_t setupOut[8];
    if ((NULL == buffer) || ((*length) > sizeof(setupOut)))
    {
        return kStatus_USB_InvalidRequest;
    }
    *buffer = setupOut;
    return kStatus_USB_Success;
}

/*!
 * @brief CDC class specific callback function.
 *
 * This function handles the CDC class specific requests.
 *
 * @param handle The USB device handle.
 * @param setup The pointer to the setup packet.
 * @param length The pointer to the length of the data buffer.
 * @param buffer The pointer to the address of setup packet data buffer.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceProcessClassRequest(usb_device_handle handle,
                                           usb_setup_struct_t *setup,
                                           uint32_t *length,
                                           uint8_t **buffer)
{
    return USB_DeviceCdcVcomClassRequest(handle, setup, length, buffer);
}


/*!
 * @brief Application initialization function.
 *
 * This function initializes the application.
 *
 * @return None.
 */
void APPInit(void)
{
    USB_DeviceClockInit();

#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
    SYSMPU_Enable(SYSMPU, 0);
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

    g_composite.speed = USB_SPEED_FULL;
    g_composite.attach = 0;
    g_composite.deviceHandle = NULL;

    if (kStatus_USB_Success != USB_DeviceInit(CONTROLLER_ID, USB_DeviceCallback, &g_composite.deviceHandle))
    {
        usb_echo("USB device composite demo init failed\r\n");
        return;
    }
    else
    {
        usb_echo("USB device composite demo\r\n");
        usb_echo("CDC class with Interrupt EP: The maximum value of the macro USB_DEVICE_CONFIG_CDC_ACM is 7.\r\n");
        usb_echo("CDC class without Interrupt EP: The maximum value of the macro USB_DEVICE_CONFIG_CDC_ACM is 15.\r\n");
        usb_echo("\r\n");

        if(USB_CDC_CIC_INTERRUPT_IN_ENDPOINT_ENABLE && USB_DEVICE_CONFIG_CDC_ACM >7 )
        {
        	usb_echo("Enable CDC class interrupt endpoint.\r\n");
        	usb_echo("Wrong parameter!!! The value of the macro USB_DEVICE_CONFIG_CDC_ACM cannot be greater than 7.\r\n");

        }
        else if(USB_CDC_CIC_INTERRUPT_IN_ENDPOINT_ENABLE == 0 && USB_DEVICE_CONFIG_CDC_ACM >15 )
        {
        	usb_echo("Disable CDC class interrupt endpoint.\r\n");
        	usb_echo("Wrong parameter!!! The value of the macro USB_DEVICE_CONFIG_CDC_ACM cannot be greater than 15.\r\n");
        }
        else
        {
	#if USB_CDC_CIC_INTERRUPT_IN_ENDPOINT_ENABLE
			usb_echo("USB CDC class with Interrupt endpoint\r\n");
	#else
			usb_echo("USB CDC class without Interrupt endpoint \r\n");
	#endif
			usb_echo("USB -> %d VCOM\r\n",USB_DEVICE_CONFIG_CDC_ACM);
        }

    }

    USB_DeviceCdcVcomInit(&g_composite);

    USB_DeviceIsrEnable();

    USB_DeviceRun(g_composite.deviceHandle);
}

/*!
 * @brief Application task function.
 *
 * This function runs the task for application.
 *
 * @return None.
 */
void APPTask(void)
{
    USB_DeviceCdcVcomTask();
}

void USB_DescriptorInit(void)
{
    uint8_t *p = NULL;
    uint8_t i;

    /* copy configuration descriptor */
    memcpy(g_UsbDeviceConfigurationDescriptor + 0, g_CdcConfigurationDescriptorTemplate, USB_DESCRIPTOR_LENGTH_CONFIGURE);

    /* copy cdc iap, interface, endpoint descriptor */
    for(i = 0; i < USB_DEVICE_CONFIG_CDC_ACM ; i++)
    {
    	memcpy(g_UsbDeviceConfigurationDescriptor + USB_DESCRIPTOR_LENGTH_CONFIGURE + USB_CDC_DESCRIPTOR_INSTANCE_LENGTH * i,
    		   g_CdcDescriptorTemplate,
			   USB_CDC_DESCRIPTOR_INSTANCE_LENGTH
				);
    }

    /* update interface and endpoint descirptor */
    for(i = 0; i < USB_DEVICE_CONFIG_CDC_ACM ; i++)
    {
    	p = g_UsbDeviceConfigurationDescriptor + USB_DESCRIPTOR_LENGTH_CONFIGURE + USB_CDC_DESCRIPTOR_INSTANCE_LENGTH * i;
    	/* The first interface number associated with this function */
    	p[2] = g_CdcVcomCicInterfaceIndex[i];
    	p += USB_IAD_DESC_SIZE;
    	/* CIC interface index */
    	p[2] = g_CdcVcomCicInterfaceIndex[i];
    	p += USB_DESCRIPTOR_LENGTH_INTERFACE;
    	p += USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC;
    	p += USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG;
    	p += USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT;

    	p[3] = g_CdcVcomCicInterfaceIndex[i];
    	p[4] = g_CdcVcomDicInterfaceIndex[i];

    	p += USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC;

    	/* USB_DESCRIPTOR_LENGTH_ENDPOINT, No interrupt endpoint */
#if USB_CDC_CIC_INTERRUPT_IN_ENDPOINT_ENABLE
    	p[2] = g_CdcVcomCicInterruptInEndpoint[i] | (USB_IN <<7);
    	p += USB_DESCRIPTOR_LENGTH_CDC_CIC_INTERRUPT_ENDPOINT;
#endif
    	/* data interface descriptor */
    	p[2] = g_CdcVcomDicInterfaceIndex[i];

    	/* bulk in endpoint descriptor */
    	p += USB_DESCRIPTOR_LENGTH_INTERFACE;
    	p[2] = g_CdcVcomDicBulkInEndpoint[i] | (USB_IN <<7);

    	/* bulk out endpoint descriptor */
    	p += USB_DESCRIPTOR_LENGTH_ENDPOINT;
    	p[2] = g_CdcVcomDicBulkOutEndpoint[i] | (USB_OUT <<7);


    }

}



#if defined(__CC_ARM) || (defined(__ARMCC_VERSION)) || defined(__GNUC__)
int main(void)
#else
void main(void)
#endif
{
	BOARD_InitPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();

    USB_CdcVcomInterfaceIndexInit();
    USB_CdcVcomEndpointInit();
    USB_DescriptorInit();

    APPInit();

    while (1)
    {
#if USB_DEVICE_CONFIG_USE_TASK
        USB_DeviceTaskFn(g_composite.deviceHandle);
#endif
        APPTask();
    }
}
