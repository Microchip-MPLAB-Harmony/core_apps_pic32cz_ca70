/*******************************************************************************
 System Tasks File

  File Name:
    tasks.c

  Summary:
    This file contains source code necessary to maintain system's polled tasks.

  Description:
    This file contains source code necessary to maintain system's polled tasks.
    It implements the "SYS_Tasks" function that calls the individual "Tasks"
    functions for all polled MPLAB Harmony modules in the system.

  Remarks:
    This file requires access to the systemObjects global data structure that
    contains the object handles to all MPLAB Harmony module objects executing
    polled in the system.  These handles are passed into the individual module
    "Tasks" functions to identify the instance of the module to maintain.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *******************************************************************************/
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include "configuration.h"
#include "definitions.h"
#include "sys_tasks.h"


// *****************************************************************************
// *****************************************************************************
// Section: RTOS "Tasks" Routine
// *****************************************************************************
// *****************************************************************************
static void lDRV_MEMORY_1_Tasks(  void *pvParameters  )
{
    while(true)
    {
        DRV_MEMORY_Tasks(sysObj.drvMemory1);
        vTaskDelay(DRV_MEMORY_RTOS_DELAY_IDX1 / portTICK_PERIOD_MS);
    }
}

static void lDRV_MEMORY_0_Tasks(  void *pvParameters  )
{
    while(true)
    {
        DRV_MEMORY_Tasks(sysObj.drvMemory0);
        vTaskDelay(DRV_MEMORY_RTOS_DELAY_IDX0 / portTICK_PERIOD_MS);
    }
}


/* Handle for the APP_SST26_Tasks. */
TaskHandle_t xAPP_SST26_Tasks;



static void lAPP_SST26_Tasks(  void *pvParameters  )
{   
    while(true)
    {
        APP_SST26_Tasks();
        vTaskDelay(10U / portTICK_PERIOD_MS);
    }
}

/* Handle for the APP_NVM_Tasks. */
TaskHandle_t xAPP_NVM_Tasks;



static void lAPP_NVM_Tasks(  void *pvParameters  )
{   
    while(true)
    {
        APP_NVM_Tasks();
        vTaskDelay(10U / portTICK_PERIOD_MS);
    }
}

/* Handle for the APP_MONITOR_Tasks. */
TaskHandle_t xAPP_MONITOR_Tasks;



static void lAPP_MONITOR_Tasks(  void *pvParameters  )
{   
    while(true)
    {
        APP_MONITOR_Tasks();
        vTaskDelay(10U / portTICK_PERIOD_MS);
    }
}




// *****************************************************************************
// *****************************************************************************
// Section: System "Tasks" Routine
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void SYS_Tasks ( void )

  Remarks:
    See prototype in system/common/sys_module.h.
*/
void SYS_Tasks ( void )
{
    /* Maintain system services */
    

    /* Maintain Device Drivers */
        (void)xTaskCreate( lDRV_MEMORY_1_Tasks,
        "DRV_MEM_1_TASKS",
        DRV_MEMORY_STACK_SIZE_IDX1,
        (void*)NULL,
        DRV_MEMORY_PRIORITY_IDX1 ,
        (TaskHandle_t*)NULL
    );

    (void)xTaskCreate( lDRV_MEMORY_0_Tasks,
        "DRV_MEM_0_TASKS",
        DRV_MEMORY_STACK_SIZE_IDX0,
        (void*)NULL,
        DRV_MEMORY_PRIORITY_IDX0 ,
        (TaskHandle_t*)NULL
    );



    /* Maintain Middleware & Other Libraries */
    

    /* Maintain the application's state machine. */
    
    /* Create OS Thread for APP_SST26_Tasks. */
    (void) xTaskCreate(
           (TaskFunction_t) lAPP_SST26_Tasks,
           "APP_SST26_Tasks",
           1024,
           NULL,
           2U ,
           &xAPP_SST26_Tasks);

    /* Create OS Thread for APP_NVM_Tasks. */
    (void) xTaskCreate(
           (TaskFunction_t) lAPP_NVM_Tasks,
           "APP_NVM_Tasks",
           1024,
           NULL,
           2U ,
           &xAPP_NVM_Tasks);

    /* Create OS Thread for APP_MONITOR_Tasks. */
    (void) xTaskCreate(
           (TaskFunction_t) lAPP_MONITOR_Tasks,
           "APP_MONITOR_Tasks",
           1024,
           NULL,
           1U ,
           &xAPP_MONITOR_Tasks);



    /* Start RTOS Scheduler. */
    
     /**********************************************************************
     * Create all Threads for APP Tasks before starting FreeRTOS Scheduler *
     ***********************************************************************/
    vTaskStartScheduler(); /* This function never returns. */

}

/*******************************************************************************
 End of File
 */

