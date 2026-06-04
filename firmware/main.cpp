#define NO_PICO_LED

#include <stdio.h>
#include <string.h>
#include <pico/stdlib.h>
#include <pico/binary_info.h>
#include <pico/multicore.h>
#include <ff.h>
#include <hw_config.h>
#include "acsi.h"
#include "image.h"
#include "Usb/usb.h"
//#include "FatFs/ff.h"
#include "config.h"

/*
	Bugs:
	SCSI UNIT ATTENTION doesn't seem to work on pc side.
	Extended partitions are not exposed to USB - this is caused by inconsistencies between how Atari and
	other operating systems handle sector clusters. Can be solved by using drivers that do the right thing.

	Known issues:
	Having both ACSI and USB as WRITEABLE works as long as you are careful to not write to disk from
		both sides at the same time. As should be obvious. This is impossible to fix, don't even go there!
		To make mathers worse, Windows wants to create a "System Volume Information" folder on every drive
		it has access to, even USB sticks. And then Windows frequently updates this folder so it is doomed
		to destroy any drive that have both sides as WRITEABLE.
		One way to avoit this, is to create an empty file in the root of every drive and call it
		"System Volume Information", and Windows will be unable to write that information.
		
	Testing:
	Need to test a lot more HD configurations.
	ICD test drive writes to boot sector and destroys drive... Could have been a virus on my test drive...
	
	Cleanup:
	image.h and image.cpp contains a many classes that should be in their own files.
	usb.h and usb.cpp contains a serial class that should have its own files.
	Maybe a better folder hierarchy?

	Optimizations:
	Double buffered DMA for acsi read/write
		https://github.com/raspberrypi/pico-examples/blob/master/pio/logic_analyser/logic_analyser.c
*/

namespace
{
	static Config config;
	static DriveCollection drives;
	static Acsi acsi;
	static int dbg_code = 0;
};

	
void core1_main(void)
{	
	acsi.Start();
}

void SetDbgCode(int v)
{
	dbg_code = v;
}

void ShowDbgCode(int led_pin)
{
	while(true)
	{
		// led unlit for two seconds
		gpio_put(led_pin, 0);
		busy_wait_ms(2000);
		int a = 0;
		for (int i = 0; i <= 4; ++i)
		{
			int b = (dbg_code >> i) & 1;	// new state of led
			if (a == b)
			{
				// Next state is the same, short blink to mark
				gpio_put(led_pin, 1 - a);
				busy_wait_ms(250);
			}
			gpio_put(led_pin, b);
			busy_wait_ms(500);
			a = b;
		}
	}
}

int main(int argc, char **argv)
{
	int result = -1;
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 0);

	bi_decl(bi_program_description("Atari ST programming help."));

    sd_card_t *pSD = sd_get_by_num(0);
	FRESULT mountResult = f_mount(&pSD->fatfs, pSD->pcName, 1);	// Mounts the SD card at path "".
	if (FR_OK == mountResult)
	{
		SetDbgCode(1);
		if (config.Load("config.txt"))
		{
			SetDbgCode(2);
			for (int i = 0; i < config.GetDrives(); ++i)
			{
				if (false == drives.LoadImage(config.GetDrive(i)))
				{
					// Failed!
				}
			}
			Serial serial(&config);
			InitUSB(drives.GetUSBInterface());
			if (acsi.Init(drives.GetAcsiInterface(), config.GetBaseId()))
			{
				SetDbgCode(3);
				multicore_launch_core1(core1_main);
				USB_main();
				acsi.Stop();
				SetDbgCode(15); // Dbg code 1111
			}
		}
		f_unmount(pSD->pcName);
	}
	ShowDbgCode(LED_PIN);
	return result;
}	
