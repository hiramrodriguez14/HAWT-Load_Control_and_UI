/*#include "ti_msp_dl_config.h"
#include "ff.h"
#include <string.h>
#include <stdint.h>


static FATFS fs;
static FIL fil;


volatile uint32_t g_fsStatus = 0;
volatile FRESULT g_mountResult = FR_OK;
volatile FRESULT g_openResult  = FR_OK;
volatile FRESULT g_lseekResult = FR_OK;
volatile FRESULT g_writeResult = FR_OK;
volatile FRESULT g_syncResult  = FR_OK;
volatile FRESULT g_closeResult = FR_OK;
volatile UINT    g_bytesWritten = 0;


/*
g_fsStatus:
0      = start
1      = mount ok
2      = open ok
3      = lseek ok
4      = write ok
5      = sync ok
6      = close ok


0xE1   = mount fail
0xE2   = open fail
0xE3   = lseek fail
0xE4   = write fail
0xE5   = sync fail
0xE6   = close fail



static void all_leds_off(void)
{
    LED1 active-low -> set = OFF 
   DL_GPIO_setPins(GPIO_GRP_1_LED1_PORT, GPIO_GRP_1_LED1_PIN);


   RGB active-high -> clear = OFF 
   DL_GPIO_clearPins(GPIO_GRP_1_RED_PORT,   GPIO_GRP_1_RED_PIN);
   DL_GPIO_clearPins(GPIO_GRP_1_GREEN_PORT, GPIO_GRP_1_GREEN_PIN);
   DL_GPIO_clearPins(GPIO_GRP_1_BLUE_PORT,  GPIO_GRP_1_BLUE_PIN);
}


static void rgb_off(void)
{
   DL_GPIO_clearPins(GPIO_GRP_1_RED_PORT,   GPIO_GRP_1_RED_PIN);
   DL_GPIO_clearPins(GPIO_GRP_1_GREEN_PORT, GPIO_GRP_1_GREEN_PIN);
   DL_GPIO_clearPins(GPIO_GRP_1_BLUE_PORT,  GPIO_GRP_1_BLUE_PIN);
}




static void rgb_red(void)
{
   rgb_off();
   DL_GPIO_setPins(GPIO_GRP_1_RED_PORT, GPIO_GRP_1_RED_PIN);
}


static void rgb_green(void)
{
   rgb_off();
   DL_GPIO_setPins(GPIO_GRP_1_GREEN_PORT, GPIO_GRP_1_GREEN_PIN);
}


static void rgb_blue(void)
{
   rgb_off();
   DL_GPIO_setPins(GPIO_GRP_1_BLUE_PORT, GPIO_GRP_1_BLUE_PIN);
}


static void rgb_yellow(void)
{
   rgb_off();
   DL_GPIO_setPins(GPIO_GRP_1_RED_PORT,   GPIO_GRP_1_RED_PIN);
   DL_GPIO_setPins(GPIO_GRP_1_GREEN_PORT, GPIO_GRP_1_GREEN_PIN);
}


static void rgb_cyan(void)
{
   rgb_off();
   DL_GPIO_setPins(GPIO_GRP_1_GREEN_PORT, GPIO_GRP_1_GREEN_PIN);
   DL_GPIO_setPins(GPIO_GRP_1_BLUE_PORT,  GPIO_GRP_1_BLUE_PIN);
}


static void rgb_magenta(void)
{
   rgb_off();
   DL_GPIO_setPins(GPIO_GRP_1_RED_PORT,  GPIO_GRP_1_RED_PIN);
   DL_GPIO_setPins(GPIO_GRP_1_BLUE_PORT, GPIO_GRP_1_BLUE_PIN);
}


static void rgb_white(void)
{
   rgb_off();
   DL_GPIO_setPins(GPIO_GRP_1_RED_PORT,   GPIO_GRP_1_RED_PIN);
   DL_GPIO_setPins(GPIO_GRP_1_GREEN_PORT, GPIO_GRP_1_GREEN_PIN);
   DL_GPIO_setPins(GPIO_GRP_1_BLUE_PORT,  GPIO_GRP_1_BLUE_PIN);
}


static void error_loop(uint32_t code)
{
   g_fsStatus = code;


   LED1 OFF durante error 
   DL_GPIO_setPins(GPIO_GRP_1_LED1_PORT, GPIO_GRP_1_LED1_PIN);


   switch (code) {
   case 0xE1:
        mount fail -> RED 
       rgb_red();
       break;


   case 0xE2:
       open fail -> GREEN 
       rgb_green();
       break;


   case 0xE3:
        lseek fail -> BLUE 
       rgb_blue();
       break;


   case 0xE4:
        write fail -> YELLOW 
       rgb_yellow();
       break;


   case 0xE5:
        sync fail -> CYAN 
       rgb_cyan();
       break;


   case 0xE6:
       close fail -> MAGENTA 
       rgb_magenta();
       break;


   default:
       unknown -> WHITE 
       rgb_white();
       break;
   }


   while (1) {
       __NOP();
   }
}




int main(void)
{
   const char *text = "hola desde MSPM0\r\n";


   SYSCFG_DL_init();


  
   DL_GPIO_setPins(GPIO_GRP_0_PORT, GPIO_GRP_0_CS_PIN);


   all_leds_off();


//     Delay extra after reset 
//    delay_cycles(3200000);    ~100 ms 
//
//     Optional unmount 
//    f_mount(0, "", 1);


 
   g_mountResult = f_mount(&fs, "", 1);
   if (g_mountResult != FR_OK) {
       delay_cycles(3200000);
       g_mountResult = f_mount(&fs, "", 1);
       if (g_mountResult != FR_OK) {
           error_loop(0xE1);
       }
   }
   g_fsStatus = 1;


   LED1 ON (active-low) 
   DL_GPIO_clearPins(GPIO_GRP_1_LED1_PORT, GPIO_GRP_1_LED1_PIN);



   g_openResult = f_open(&fil, "test.txt", FA_OPEN_ALWAYS | FA_WRITE);
   if (g_openResult != FR_OK) {
       error_loop(0xE2);
   }
   g_fsStatus = 2;


  
   DL_GPIO_setPins(GPIO_GRP_1_LED1_PORT, GPIO_GRP_1_LED1_PIN);
   rgb_red();


  
   g_lseekResult = f_lseek(&fil, f_size(&fil));
   if (g_lseekResult != FR_OK) {
       f_close(&fil);
       error_loop(0xE3);
   }
   g_fsStatus = 3;


  
   rgb_green();


 
   g_writeResult = f_write(&fil, text, (UINT)strlen(text), (UINT *)&g_bytesWritten);
   if ((g_writeResult != FR_OK) || (g_bytesWritten != (UINT)strlen(text))) {
       f_close(&fil);
       error_loop(0xE4);
   }
   g_fsStatus = 4;



   rgb_blue();



   g_syncResult = f_sync(&fil);
   if (g_syncResult != FR_OK) {
       f_close(&fil);
       error_loop(0xE5);
   }
   g_fsStatus = 5;



   rgb_yellow();


   g_closeResult = f_close(&fil);
   if (g_closeResult != FR_OK) {
       error_loop(0xE6);
   }
   g_fsStatus = 6;



   rgb_off();
   DL_GPIO_clearPins(GPIO_GRP_1_LED1_PORT, GPIO_GRP_1_LED1_PIN);


   while (1) {
       __NOP();
   }
}
*/