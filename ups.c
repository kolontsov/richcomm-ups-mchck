// Simple re-implementation of Richcomm UPS interface. Target board is MC HCK (https://mchck.org/about/)
// Protocol details are taken from drivers/richcomm_usb.c of Network UPS Tools (http://www.networkupstools.org/)
//
// More info at http://github.com/kolontsov/richcomm-ups-mchck/
// This source code is in public domain (http://unlicense.org/) 

#include <mchck.h>

#define RCM_VENDOR  0x0925	// This Vendor ID is from "USB Complete" book by Jan Axelson;
#define RCM_PRODUCT 0x1234	// and mistakenly(?) used by Richcomm programmers. Don't act like that.
				// BTW: http://www.oshwa.org/2013/11/19/new-faq-on-usb-vendor-id-and-product-id/

#define RCM_CLASS    0xFF	// Device class (0xFF - vendor specific)
#define RCM_SUBCLASS 0		// Doesn't matter for us
#define RCM_PROTOCOL 0		// Same here

#define UPS_REPLY_EP	  0x81	// Endpoint address; high bit = 1 (direction: dev->host), ep_num = 1
#define UPS_REPLY_EP_SIZE 32	// Maximum packet size for EP1 IN

#define UPS_REQUESTTYPE  0x21	// Details of message which we expect from host (recepient: interface; type: class req)
#define UPS_REQUESTVALUE 0x9
#define UPS_MESSAGEVALUE 0x200
#define UPS_INDEXVALUE   0

#define UPS_REQUESTSIZE	4	// Size of request and reply
#define UPS_REPLYSIZE	6

static unsigned char ups_reply[UPS_REPLYSIZE]     = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

// 3rd bit of 4th byte indicates whether the UPS is on line (1) or on battery (0)
static void ups_online(int online) 
{
	if (online)
		ups_reply[3] |= 0x4;
	else
		ups_reply[3] &= ~0x4 | (online << 2);
}

// 2nd bit of 4th byte indicates battery status; normal (1) or low (0)
static void ups_batterystatus(int good) 
{
	if (good)
		ups_reply[3] |= 0x2;
	else
		ups_reply[3] &= ~0x2;
}


static void rcm_init(int config);
static int rcm_handle_control(struct usb_ctrl_req_t *req, void *data);
static void rcm_handle_data(void *buf, ssize_t len, void *data);

// Define all-in-one structure for configuration, interface and endpoint descriptors 
struct function_desc {
        struct usb_desc_iface_t iface;
        struct usb_desc_ep_t	int_in_ep;
} __packed;

struct usb_config_1 {
        struct usb_desc_config_t config;
        struct function_desc	 usb_function_0;
} __packed;

static const struct usb_config_1 usb_config_1 = {
	.config = {
		.bLength = sizeof(struct usb_desc_config_t),
		.bDescriptorType = USB_DESC_CONFIG,
		.wTotalLength = sizeof(struct usb_config_1),
		.bNumInterfaces = 1,		// Number of interfaces
		.bConfigurationValue = 1,	// Value to use as an argument to select this configuration
						// ((setConfiguration(0) is used to 'unconfigure' device)
		.iConfiguration = 0,		// Index of string descriptor describing this configuration
		.one = 1,			// Reserved bit
		.bMaxPower = 10			// Maximum power consumption in 2mA units
	},
	.usb_function_0 = {
                // Control interface
		.iface = {
			.bLength = sizeof(struct usb_desc_iface_t),
			.bDescriptorType = USB_DESC_IFACE,
			.bInterfaceNumber = 0,		// Interface number
			.bAlternateSetting = 0,		// Value used to select alternative setting
			.bNumEndpoints = 1,		// Don't count control endpoint zero
			.iInterface = 0,		// Index of string descriptor for interface name
			.bInterfaceClass = RCM_CLASS,	// Class/subclass/protocol
			.bInterfaceSubClass = RCM_SUBCLASS,
			.bInterfaceProtocol = RCM_PROTOCOL,
			.iInterface = 0			// Number of endpoints used for this interface
		}, 
		// Endpoint 0x81: interrupt IN
                .int_in_ep = {
                        .bLength = sizeof(struct usb_desc_ep_t),
                        .bDescriptorType = USB_DESC_EP,
                        .bEndpointAddress = UPS_REPLY_EP,
                        .type = USB_EP_INTR,
                        .wMaxPacketSize = UPS_REPLY_EP_SIZE,
                        .bInterval = 0xFF	// maximum value for polling interval (in frames)
                }
	},
};

// Specify handler for non-standard control messages
static const struct usbd_function usbd_function = {
	.control = rcm_handle_control,
	.interface_count = 1
};

// This structure is needed by usb_attach_function() and usb_init_ep()
static struct usbd_function_ctx_header usbd_ctx;

// Device configuration N1
static const struct usbd_config usbd_config_1 = {
	.init = rcm_init,			// Initialization callback
	.desc = &usb_config_1.config		// Pointer to configuration descriptor
};

// Device Descriptor
static const struct usb_desc_dev_t device_dev_desc = {
	.bLength = sizeof(struct usb_desc_dev_t),
	.bDescriptorType = USB_DESC_DEV,
	.bcdUSB = { .maj = 2 },			// USB revision 2.0
	.bDeviceClass = USB_DEV_CLASS_SEE_IFACE,// Class/subclass/proto are defined in interface descriptor
	.bDeviceSubClass = USB_DEV_SUBCLASS_SEE_IFACE,
	.bDeviceProtocol = USB_DEV_PROTO_SEE_IFACE,
	.bMaxPacketSize0 = EP0_BUFSIZE,		// Max size for endpoint zero (for control messages)
	.idVendor = RCM_VENDOR,
	.idProduct = RCM_PRODUCT,
	.bcdDevice = { .sub = 1 },		// Device release number (0.01)
	.iManufacturer = 1,			// Index of string descriptor, can be zero if not used
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,		// Number of possible configurations
};

// Strings Descriptor
static const struct usb_desc_string_t * const device_str_desc[] = {
	USB_DESC_STRING_LANG_ENUS,		  // 2-byte American English language ID (0x409)
	USB_DESC_STRING(u"kolontsov.com"),	  // index = 1
	USB_DESC_STRING(u"Richcomm UPS emulator"),// index = 2
	USB_DESC_STRING_SERIALNO,                 // index = 3
	NULL
};

// Pointer to device descriptor, string descriptor and configurations; used by usb_init()
const struct usbd_device rcm_device = {
	.dev_desc = &device_dev_desc,
	.string_descs = device_str_desc,
	.configs = {
		&usbd_config_1,
		NULL
	}
};

// Pipe for sending messages from device to host
static struct usbd_ep_pipe_state_t *tx_pipe;

// Callback from device initialization
static void rcm_init(int config) 
{
	static int initialized = 0;

	// Current (Aug'14) mchck's usb implementation doesn't handle well situations with
	// multiple transitions from/to unconfigured state (by USB-IF test suite, for example)
	if (!initialized) {
		initialized = 1;
		usb_attach_function(&usbd_function, &usbd_ctx);

		// Initialize pipe (for sending 'interrup in' data).Args
		//  - struct ubd_functions_ctx_header *ctx
		//  - EP number (1 is data channel, see EP definition)
		//  - direction: USB_EP_TX, tx from device (for host INTERRUPT IN)
		//  - max buffer size
		tx_pipe = usb_init_ep(&usbd_ctx, 1, USB_EP_TX, UPS_REPLY_EP_SIZE);
	}
}

// Try to handle request; return != 0 if handled. Function called by standard handler for unknown requests
static int rcm_handle_control(struct usb_ctrl_req_t *req, void *data)
{
	static unsigned char buf[UPS_REQUESTSIZE];

	if (req->recp     == USB_CTRL_REQ_IFACE &&    // look cleaner than 'bmRequest == UPS_REQUESTTYPE' (0x21)
	    req->type     == USB_CTRL_REQ_CLASS &&
	    req->bRequest == UPS_REQUESTVALUE && 
	    req->wValue   == UPS_MESSAGEVALUE && 
	    req->wIndex   == UPS_INDEXVALUE && 
	    req->wLength  == UPS_REQUESTSIZE) 
	{
		// Now we need to read payload
		usb_ep0_rx(buf, req->wLength, rcm_handle_data, NULL);

		// 'Mark' request as handled, but don't send ACK yet (see 'rcm_handle_data' callback)
		return (1);
	}

	// Unknown request, pass through
	return 0;
}

// Handle request's payload and send answer
static void rcm_handle_data(void *buf, ssize_t len, void *data) 
{
	// Demonstration 
	static int counter = 0;
	switch (counter++) {
		case 0:  ups_online(1); ups_batterystatus(1); break;
		case 30: ups_online(0); break;
		case 40: ups_online(1); break;
		case 50: ups_online(0); break;
		case 60: ups_batterystatus(0); break;
	}

	onboard_led(ONBOARD_LED_TOGGLE);

	// Send ACK for this request
	usb_handle_control_status(0);

	// Send UPS data to endpoint 1
	usb_tx(tx_pipe, ups_reply, UPS_REPLYSIZE, UPS_REPLY_EP_SIZE, NULL, NULL);
}


void main() 
{
	usb_init(&rcm_device);

	// Wait for interrupts
	sys_yield_for_frogs();	
}
