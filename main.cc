#include "sleep.h"
#include "xil_printf.h"
#include "stdio.h"
#include "dma_intr.h"
#include "sys_intr.h"
#include "xgpio.h"
#include "xil_io.h"
#include "main_functions.h"
#include "gpio_init.h"
/************************** Constant Definitions *****************************/
#define PD_Threshold_LOW	560			// 33mv
#define PD_Threshold_HIGH	575			//100mv


//for UHF   test: 519:0v  520:0.009775    521:0.01955    522:0.029325
//#define PD_Threshold_LOW	519			// 0v
//#define PD_Threshold_HIGH	523			//33mv

/************************** Function Prototypes ******************************/

void dma_gpio_init(void);
void dma_wr_set(u32 set);
int init_intr_sys(void);
int axi_dma_transfer_test(void);
float to_vol(u32 x);
int NN_64_PRPD_IMG(void);
void print_prpd_data(void);

/************************** Variable Definitions *****************************/

static XAxiDma Axidma;     //XAxiDmaʵ��
static XScuGic Intc;       //�жϿ�������ʵ��
XGpioPs gpio;

u32 RxBufferPtr[100];
u32 *rx_buffer_ptr[100];

//volatile u8 PRPD_IMG[360][50] = {0};

uint8_t NN_PRPD_IMG[64][64] = {0};

int count = 0;

extern volatile int RxDone;
extern volatile int Error;
extern volatile int key_status;
extern volatile int led_status;

volatile u32 pkg_cnt;
volatile u32 first_transmit;
volatile u32 pkg_cntkg_cnt=0;
//u32 i=0;

XGpio gpio_user_rstn;
XGpio gpio_user_start;

int	PRPD_ADDR = 0x01900000;	//0x01900000

/************************** Function Definitions *****************************/

int main(void)
{
	xil_printf("program begin\r\n");
	xil_printf("\r\n");
	dma_gpio_init();
	init_intr_sys();
//	setup();
//	loop();
	axi_dma_transfer_test();
	count = NN_64_PRPD_IMG();
	print_prpd_data();
	if(count == 0 || count > 30000)
	{
		xil_printf("Recognized failed!   \n");
	}
	else
	{
		setup();
		loop();
	}
}


int axi_dma_transfer_test()
{
	xil_printf("enter axi_dma_transfer_test \r\n");
	u32 done_t = 1;
	u32 Status;
	pkg_cnt = 0;
	first_transmit =1;

	for(u32 i=0; i<100; i++)
	{
		rx_buffer_ptr[i] = (u32 *) (RX_BUFFER_BASE + 0x00300000*i);
	}

	while(done_t)
	{
		if(key_status)
		{
			if(first_transmit)
					{
						dma_wr_set(1);//start dma

						key_status = FALSE;

						usleep(30000);

						Status = XAxiDma_SimpleTransfer(&Axidma,(UINTPTR) rx_buffer_ptr[pkg_cnt],
								MAX_PKT_LEN, XAXIDMA_DEVICE_TO_DMA);

						first_transmit = 0;



						while (!RxDone && !Error)
												        ;
						dma_wr_set(0);//stop dma
						XGpioPs_IntrClearPin(&gpio, SYNC_SIGNAL_INPUT); //������� KEY �ж�
						XGpioPs_IntrEnablePin(&gpio, SYNC_SIGNAL_INPUT);
					}
					else if(RxDone && pkg_cnt < 99) // Դ 100
					{
						dma_wr_set(1);//start dma

						key_status = FALSE;

						usleep(30000);

						RxDone =0;

						Status = XAxiDma_SimpleTransfer(&Axidma,(UINTPTR) rx_buffer_ptr[pkg_cnt+1],
								MAX_PKT_LEN, XAXIDMA_DEVICE_TO_DMA);

//						dma_wr_set(0);//stop dma

						Xil_DCacheFlushRange((UINTPTR) rx_buffer_ptr[pkg_cnt], MAX_PKT_LEN);

						pkg_cnt++;

						while (!RxDone && !Error)
												        ;
						dma_wr_set(0);//stop dma
						XGpioPs_IntrClearPin(&gpio, SYNC_SIGNAL_INPUT); //������� KEY �ж�
						XGpioPs_IntrEnablePin(&gpio, SYNC_SIGNAL_INPUT);
					}
					else if(RxDone && pkg_cnt == 99)
					{
						key_status = FALSE;

						Xil_DCacheFlushRange((UINTPTR) rx_buffer_ptr[pkg_cnt], MAX_PKT_LEN);

						RxDone =0;

						pkg_cnt = 0;

						dma_wr_set(0);//stop

						done_t = 0;

					}
		}
	}

//	return XST_SUCCESS;
}


void dma_gpio_init(void)
{
	XGpio_Initialize(&gpio_user_rstn, XPAR_GPIO_USER_RST_DEVICE_ID);
	XGpio_SetDataDirection(&gpio_user_rstn, 1, 0x0);
	XGpio_DiscreteWrite(&gpio_user_rstn,1,0x0);

	XGpio_Initialize(&gpio_user_start, XPAR_GPIO_USER_START_DEVICE_ID);
	XGpio_SetDataDirection(&gpio_user_start, 1, 0x0);

	XGpio_DiscreteWrite(&gpio_user_rstn,1,0x1);//reset done
}

void dma_wr_set(u32 set)
{
	if(set==0)
		XGpio_DiscreteWrite(&gpio_user_start, 1, 0x0);//start dma
	else
		XGpio_DiscreteWrite(&gpio_user_start, 1, 0x1);//start dma
}

int init_intr_sys(void)
{
	Init_Intr_System(&Intc); // initial ScuGic interrupt system
	Setup_Intr_Exception(&Intc);
	DMA_Intr_Init(&Axidma,0);//initial interrupt system
	Gpiops_init(&gpio);
//	Init_Intr_System(&Intc); // initial ScuGic interrupt system
	GpioPs_Setup_Intr_System(&Intc, &gpio, GPIO_INTERRUPT_ID);
//	Setup_Intr_Exception(&Intc);
	DMA_Setup_Intr_System(&Intc,&Axidma,RX_INTR_ID);//setup dma interrpt system
	DMA_Intr_Enable(&Intc,&Axidma);

	return 0;
}

float to_vol(u32 x)
{
    float y = 0;
    if(x == 0)
    {
    	y = 0;
    }
    else
    {
    	y = (float) ((10.0*x / 1023.0) - 5.17);
    }

    return y;
}


//	vol = (10*x / 1023) - 5.17

int NN_64_PRPD_IMG()
{
    // private variable
    u32 global_pk = 0;
    u32 p_max = 0;
    int pd_flag = 0;
    int p_count = 0;
    int p_max_vol_nor = 0;
    int total_count = 0;
    int img_max = 0;
    int NN_PRPD_IMG_32[64][64] = {0};

    xil_printf("ENTER NN_64_PRPD_IMG   \r\n");


    for(int j=0; j<100; j++)
	{
		RxBufferPtr[j] = RX_BUFFER_BASE + 0x00300000*j;  //每个工频周期的数据内存始地址，3M
	}

    //寻找最大电压值
    for(int img_num_t=0; img_num_t<100; img_num_t++)
	{
        for(int gbl_cnt=0; gbl_cnt<786432; gbl_cnt++ )
        {
            if((u32) Xil_In32(RxBufferPtr[img_num_t]+ gbl_cnt*4) > global_pk)
            {
                global_pk = (u32) Xil_In32(RxBufferPtr[img_num_t]+ gbl_cnt*4);
            }

//            if((u32) Xil_In32(RxBufferPtr[img_num]+ gbl_cnt*4) < PD_Threshold_LOW)
//            {
//                Xil_Out32(RxBufferPtr[img_num]+ gbl_cnt*4, 0);
//            }
        }

	}

    for(int img_num=0; img_num<100; img_num++)
    {
        for(int pixel_point=0; pixel_point<64; pixel_point++ )
        {
            for(int add_n=0; add_n<12288; add_n++)
            {
                if((u32) Xil_In32(RxBufferPtr[img_num]+ pixel_point*12288*4 + add_n*4) >= PD_Threshold_HIGH && pd_flag == 0)
                {
                    pd_flag = 1;
                }

                if(pd_flag == 1)
                {
                    if((u32) Xil_In32(RxBufferPtr[img_num]+ pixel_point*12288*4 + add_n*4) > p_max)
                    {
                        p_max = (u32) Xil_In32(RxBufferPtr[img_num]+ pixel_point*12288*4 + add_n*4);
                    }
                }

                if((u32) Xil_In32(RxBufferPtr[img_num]+ pixel_point*12288*4 + add_n*4) <= PD_Threshold_LOW && pd_flag == 1)
                {
                    pd_flag = 0;
                    p_count = p_count + 1;
                }
            }

            if(p_count < 5)
            {
            	p_max_vol_nor = (int) (to_vol(p_max) / to_vol(global_pk) * 63.0);
            	NN_PRPD_IMG_32[(63-p_max_vol_nor)][pixel_point] = NN_PRPD_IMG_32[(63-p_max_vol_nor)][pixel_point] + p_count;

            	total_count = total_count + p_count;
            }

            p_max = 0;
            p_count = 0;
            p_max_vol_nor = 0;
            pd_flag = 0;
        }
        pd_flag = 0;
	}

    xil_printf("Total: %d\r\n", total_count );

    //find peak value of NN_PRPD_IMG
    for(int img_row=0; img_row<64; img_row++)
    {
        for(int img_col=0; img_col<64; img_col++)
        {
            if(NN_PRPD_IMG_32[img_row][img_col] > img_max)
            {
                img_max = NN_PRPD_IMG_32[img_row][img_col];
            }
        }
    }

    for(int img_row_t=0; img_row_t<64; img_row_t++)
    {
        for(int img_col_t=0; img_col_t<64; img_col_t++)
        {
            int tmp = 0;
            tmp = (int) ((float) NN_PRPD_IMG_32[img_row_t][img_col_t] / (float) img_max * 255.0);
            NN_PRPD_IMG[img_row_t][img_col_t] = (u8) tmp;
        }
    }

    return total_count;
}

void print_prpd_data()
{
	xil_printf("the PRPD:\r\n");
	for(int A_row=0; A_row<64; A_row++)
	{
		for(int A_col=0; A_col<64; A_col++)
		{
			xil_printf("%d ,", NN_PRPD_IMG[A_row][A_col]);
		}
		xil_printf("\r\n");
	}
}

