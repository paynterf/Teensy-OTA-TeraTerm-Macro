This is a Tera Term macro file created to automate over-the-air (OTA) program updates for a Teensy microcontroller module.  It uses Joe Pasquariello's 'FlasherX' code, combined with VS2019/Visual Micro's 'board.txt' post-build command feature to launch this macro to use the Tera Term serial comms program to upload the new program to the Teensy.  

In addition to this macro, you will obviously need the Tera Term comms program.  If you have VS2019/Visual Micro as your Arduino IDE, then you can use the accompanying 'board.txt' file directly - just place it in the sketch folder.  If you aren't using VS2019/VM, then you'll need to figure out how/if your IDE has post-build command capability.

For further details, please read my 'Paynter's Palace' blog post at https://www.fpaynter.com/2021/10/over-the-air-ota-firmware-updates-for-teensy-3-4-x-part-ii/
