/******************************************************************************
* File Name: main.c
*
* Description: This is the source code for the XMC7000 MCU CANFD example 
* for ModusToolbox.
*
* Related Document: See README.md
*
*******************************************************************************
* Copyright 2022-2023, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/******************************************************************************
* Include header files
******************************************************************************/
#include <stdio.h>
#include "cy_pdl.h"
#include "cycfg.h"
#include "cybsp.h"
#include "cy_retarget_io.h"


/*******************************************************************************
* Macros
*******************************************************************************/
/* CAN node definition */
#define CAN_NODE_1              1
#define CAN_NODE_2              2
/* Set the example CAN node */
#define USE_CAN_NODE            CAN_NODE_1

/* CAN mode definition */
#define CAN_CLASSIC_MODE        0
#define CAN_FD_MODE             1
/* Set the example CAN mode */
#define USE_CAN_MODE            CAN_FD_MODE

/* CAN channel number */
#define CAN_HW_CHANNEL          1
#define CAN_BUFFER_INDEX        0
/* CAN data length code, frame has 8 data bytes in this example */
#define CAN_DLC                 8

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
/* canfd interrupt handler */
void isr_canfd (void);

/* canfd frame receive callback */
void canfd_rx_callback(bool rxFIFOMsg, uint8_t msgBufOrRxFIFONum, cy_stc_canfd_rx_buffer_t* basemsg);

/* button press interrupt handler */
void isr_button (void);

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* This structure initializes the CANFD interrupt for the NVIC */
cy_stc_sysint_t canfd_irq_cfg =
{
    .intrSrc  = (NvicMux2_IRQn << 16) | CANFD_IRQ_0, /* Source of interrupt signal */
    .intrPriority = 1U, /* Interrupt priority */
};

/* This structure initializes the button interrupt for the NVIC */
cy_stc_sysint_t button_intr_config =
{
    .intrSrc  = (NvicMux2_IRQn << 16) | CYBSP_USER_BTN_IRQ,
    .intrPriority = 0U,
};

/* This is a shared context structure, unique for each canfd channel */
cy_stc_canfd_context_t canfd_context; 

/* Variable which holds the button pressed status */
bool ButtonIntrFlag = false;

/* Array to store the data bytes of the CANFD frame */
uint32_t canfd_dataBuffer[] = 
{
    [CANFD_DATA_0] = 0x04030201U,
    [CANFD_DATA_1] = 0x08070605U,
};


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function. It initializes the CANFD channel 
* and interrupt. User button and User LED are also initialized. The main loop
* checks for the button pressed interrupt flag and when it is set, a CANFD frame
* is sent. Whenever a CANFD frame is received from other nodes, the user LED 
* toggles and the received data is logged over serial terminal.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    cy_en_canfd_status_t status;

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize retarget-io for uart logging */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, 
                                CY_RETARGET_IO_BAUDRATE);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf("===============================================================\r\n");
    printf("XMC7000 MCU: CANFD example\r\n");
    printf("===============================================================\r\n\n");

    printf("===============================================================\r\n");
#if USE_CAN_MODE == CAN_CLASSIC_MODE
    printf("Classic CAN Node-%d\r\n", USE_CAN_NODE);
#elif USE_CAN_MODE == CAN_FD_MODE
    printf("CAN FD Node-%d\r\n", USE_CAN_NODE);
#endif
    printf("===============================================================\r\n\n");

    /* Hook the interrupt service routine and enable the interrupt */
    (void) Cy_SysInt_Init(&canfd_irq_cfg, &isr_canfd);
    Cy_SysInt_Init(&button_intr_config, isr_button);
    NVIC_EnableIRQ(NvicMux2_IRQn);

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize CANFD Channel */
#if USE_CAN_MODE == CAN_CLASSIC_MODE
    CANFD_config.canFDMode = false;
#endif
    status = Cy_CANFD_Init(CANFD_HW, CAN_HW_CHANNEL, &CANFD_config, 
                           &canfd_context);
    if (status != CY_CANFD_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Setting CAN node identifier */
    CANFD_T0RegisterBuffer_0.id = USE_CAN_NODE;

    /* Assign the user defined data buffer to CANFD data area */
    CANFD_txBuffer_0.data_area_f = canfd_dataBuffer;

    for(;;)
    {
        if (ButtonIntrFlag == true)
        {
            ButtonIntrFlag = false;
            /* Sending CANFD frame to other node */
            status = Cy_CANFD_UpdateAndTransmitMsgBuffer(CANFD_HW, 
                                                    CAN_HW_CHANNEL, 
                                                    &CANFD_txBuffer_0,
                                                    CAN_BUFFER_INDEX, 
                                                    &canfd_context);
            #if USE_CAN_MODE == CAN_CLASSIC_MODE
              printf("CAN standard frame sent from Node-%d\r\n\r\n", USE_CAN_NODE);
            #elif USE_CAN_MODE == CAN_FD_MODE
              printf("CANFD frame sent from Node-%d\r\n\r\n", USE_CAN_NODE);
            #endif
        }
    }
}

/*******************************************************************************
* Function Name: isr_button
********************************************************************************
* Summary:
* This is the callback function for button press
*
* Parameters:
*    None
*    
*******************************************************************************/
void isr_button (void)
{
    uint32_t intStatus = 0;

    /* If user button falling edge detected */
    intStatus = Cy_GPIO_GetInterruptStatusMasked(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN);
    if (intStatus != 0ul)
    {
        /* Set button interrupt flag */
        ButtonIntrFlag = true;
        /* Clears the triggered pin interrupt */
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN);
    }
}

/*******************************************************************************
* Function Name: isr_canfd
********************************************************************************
* Summary:
* This is the interrupt handler function for the canfd interrupt.
*
* Parameters:
*  none
*    
*
*******************************************************************************/
void isr_canfd(void)
{
    /* Just call the IRQ handler with the current channel number and context */
    Cy_CANFD_IrqHandler(CANFD_HW, CAN_HW_CHANNEL, &canfd_context);
}

/*******************************************************************************
* Function Name: canfd_rx_callback
********************************************************************************
* Summary:
* This is the callback function for canfd reception
*
* Parameters:
*    rxFIFOMsg                      Message was received in Rx FIFO (0/1)
*    msgBufOrRxFIFONum              RxFIFO number of the received message
*    basemsg                        Message buffer
*
*******************************************************************************/
void canfd_rx_callback (bool                        rxFIFOMsg, 
                        uint8_t                     msgBufOrRxFIFONum, 
                        cy_stc_canfd_rx_buffer_t*   basemsg)
{
    /* Array to hold the data bytes of the CANFD frame */
    uint8_t canfd_data_buffer[CAN_DLC];
    /* Variable to hold the data length code of the CANFD frame */
    int canfd_dlc;
    /* Variable to hold the Identifier of the CANFD frame */
    int canfd_id;

    /* Message was received in Rx FIFO */
    if (rxFIFOMsg == true)
    {
        /* Checking whether the frame received is a data frame */
        if(CY_CANFD_RTR_DATA_FRAME == basemsg->r0_f->rtr) 
        {
            /* Toggle the user LED */
            Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
            /* Get the CAN DLC and ID from received message */
            canfd_dlc = basemsg->r1_f->dlc;
            canfd_id  = basemsg->r0_f->id;
            /* Print the received message by UART */
            printf("%d bytes received from Node-%d with identifier %d\r\n\r\n", 
                                                        canfd_dlc,
                                                        canfd_id,
                                                        canfd_id);
            memcpy(canfd_data_buffer, basemsg->data_area_f, canfd_dlc);
            printf("Rx Data : ");
            for (uint8_t msg_idx = 0; msg_idx < canfd_dlc ; msg_idx++)
            {
                printf(" %d ", canfd_data_buffer[msg_idx]);
            }
            printf("\r\n\r\n");
        }
    }
    /* These parameters are not used in this snippet */
    (void)msgBufOrRxFIFONum;
}

/* [] END OF FILE */
