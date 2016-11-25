// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.



#include "sdkconfig.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_types.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_intr.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include <limits.h>
#include <assert.h>

static const char* TAG = "intr_alloc";


#define ETS_INTERNAL_TIMER0_INTR_NO 6
#define ETS_INTERNAL_TIMER1_INTR_NO 15
#define ETS_INTERNAL_TIMER2_INTR_NO 16
#define ETS_INTERNAL_SW0_INTR_NO 7
#define ETS_INTERNAL_SW1_INTR_NO 29
#define ETS_INTERNAL_PROFILING_INTR_NO 11


/*
Define this to debug the choices made when allocating the interrupt. This leads to much debugging
output within a critical region, which can lead to weird effects like e.g. the interrupt watchdog
being triggered, that is why it is separate from the normal LOG* scheme.
*/
//define DEBUG_INT_ALLOC_DECISIONS
#ifdef DEBUG_INT_ALLOC_DECISIONS
# define ALCHLOG(...) ESP_EARLY_LOGD(TAG, __VA_ARGS__)
#else
# define ALCHLOG(...) do {} while (0)
#endif


typedef enum {
    INTDESC_NORMAL=0,
    INTDESC_RESVD,
    INTDESC_SPECIAL //for xtensa timers / software ints
} int_desc_flag_t;

typedef enum {
    INTTP_LEVEL=0,
    INTTP_EDGE,
    INTTP_NA
} int_type_t;

typedef struct {
    int level;
    int_type_t type;
    int_desc_flag_t cpuflags[2];
} int_desc_t;


//We should mark the interrupt for the timer used by FreeRTOS as reserved. The specific timer 
//is selectable using menuconfig; we use these cpp bits to convert that into something we can use in
//the table below.
#if CONFIG_FREERTOS_CORETIMER_0
#define INT6RES INTDESC_RESVD
#else
#define INT6RES INTDESC_SPECIAL
#endif

#if CONFIG_FREERTOS_CORETIMER_1
#define INT15RES INTDESC_RESVD
#else
#define INT15RES INTDESC_SPECIAL
#endif

#if CONFIG_FREERTOS_CORETIMER_2
#define INT16RES INTDESC_RESVD
#else
#define INT16RES INTDESC_SPECIAL
#endif

//This is basically a software-readable version of the interrupt usage table in include/soc/soc.h
const static int_desc_t int_desc[32]={
    { 1, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //0
    { 1, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //1
    { 1, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //2
    { 1, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //3
    { 1, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_NORMAL} }, //4
    { 1, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_NORMAL} }, //5
    { 1, INTTP_NA,    {INT6RES,        INT6RES       } }, //6
    { 1, INTTP_NA,    {INTDESC_SPECIAL,INTDESC_SPECIAL}}, //7
    { 1, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //8
    { 1, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //9
    { 1, INTTP_EDGE , {INTDESC_RESVD,  INTDESC_NORMAL} }, //10
    { 3, INTTP_NA,    {INTDESC_SPECIAL,INTDESC_SPECIAL}}, //11
    { 1, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //12
    { 1, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //13
    { 7, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //14, NMI
    { 3, INTTP_NA,    {INT15RES,       INT15RES      } }, //15
    { 5, INTTP_NA,    {INT16RES,       INT16RES      } }, //16
    { 1, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //17
    { 1, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //18
    { 2, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //19
    { 2, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //20
    { 2, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //21
    { 3, INTTP_EDGE,  {INTDESC_RESVD,  INTDESC_NORMAL} }, //22
    { 3, INTTP_LEVEL, {INTDESC_NORMAL, INTDESC_NORMAL} }, //23
    { 4, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_NORMAL} }, //24
    { 4, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //25
    { 5, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //26
    { 3, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //27
    { 4, INTTP_EDGE,  {INTDESC_NORMAL, INTDESC_NORMAL} }, //28
    { 3, INTTP_NA,    {INTDESC_SPECIAL,INTDESC_SPECIAL}}, //29
    { 4, INTTP_EDGE,  {INTDESC_RESVD,  INTDESC_RESVD } }, //30
    { 5, INTTP_LEVEL, {INTDESC_RESVD,  INTDESC_RESVD } }, //31
};


//For memory usage and to get an unique ID for every int on every CPU core, the
//intrs and cpus are stored in in one int. These functions handle that.
inline static int to_intno_cpu(int intno, int cpu) 
{
    return intno+cpu*32;
}

inline static int to_intno(int intno_cpu) 
{
    return (intno_cpu)&31;
}

inline static int to_cpu(int intno_cpu) 
{
    return (intno_cpu)/32;
}

typedef struct shared_vector_desc_t shared_vector_desc_t;
typedef struct vector_desc_t vector_desc_t;

struct shared_vector_desc_t {
    volatile uint32_t *statusreg;
    uint32_t statusmask;
    intr_handler_t isr;
    void *arg;
    shared_vector_desc_t *next;
};


#define VECDESC_FL_RESERVED     (1<<0)
#define VECDESC_FL_INIRAM       (1<<1)
#define VECDESC_FL_SHARED       (1<<2)
#define VECDESC_FL_NONSHARED    (1<<3)

struct vector_desc_t {
    int intno_cpu;                          //intno+cpu*32
    int flags;                              //OR of VECDESC_FLAG_* defines
    shared_vector_desc_t *shared_vec_info;  //used when VECDESC_FL_SHARED
    vector_desc_t *next;
};

struct int_handle_data_t {
    vector_desc_t *vector_desc;
    shared_vector_desc_t *shared_vector_desc;
};


//Linked list of vector descriptions, sorted by intno_cpu value
static vector_desc_t *vector_desc_head;

//This bitmask has an 1 if the int should be disabled when the flash is disabled.
static uint32_t non_iram_int_mask[portNUM_PROCESSORS];
//This bitmask has 1 in it if the int was disabled using esp_intr_noniram_disable.
static uint32_t non_iram_int_disabled[portNUM_PROCESSORS];
static bool non_iram_int_disabled_flag[portNUM_PROCESSORS];


static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

//Inserts an item into vector_desc list so that the list is sorted
//with an incrementing intno_cpu value.
static void insert_vector_desc(vector_desc_t *to_insert) 
{
    vector_desc_t *vd=vector_desc_head;
    vector_desc_t *prev=NULL;
    while(vd!=NULL) {
        if (vd->intno_cpu >= to_insert->intno_cpu) break;
        prev=vd;
        vd=vd->next;
    }
    if (vd==NULL && prev==NULL) {
        //First item
        vector_desc_head=to_insert;
        vector_desc_head->next=NULL;
    } else {
        prev->next=to_insert;
        to_insert->next=vd;
    }
}

//Returns a vector_desc entry for an intno/cpu, or NULL if none exists.
static vector_desc_t *find_desc_for_int(int intno, int cpu) 
{
    vector_desc_t *vd=vector_desc_head;
    while(vd!=NULL) {
        if (vd->intno_cpu==to_intno_cpu(intno, cpu)) break;
        vd=vd->next;
    }
    return vd;
}

//Returns a vector_desc entry for an intno/cpu.
//Either returns a preexisting one or allocates a new one and inserts
//it into the list.
static vector_desc_t *get_desc_for_int(int intno, int cpu) 
{
    vector_desc_t *vd=find_desc_for_int(intno, cpu);
    if (vd==NULL) {
        vector_desc_t *newvd=malloc(sizeof(vector_desc_t));
        memset(newvd, 0, sizeof(vector_desc_t));
        newvd->intno_cpu=to_intno_cpu(intno, cpu);
        insert_vector_desc(newvd);
        return newvd;
    } else {
        return vd;
    }
}

esp_err_t esp_intr_mark_shared(int intno, int cpu, bool is_int_ram)
{
    if (intno>31) return ESP_ERR_INVALID_ARG;
    if (cpu>=portNUM_PROCESSORS) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&spinlock);
    vector_desc_t *vd=get_desc_for_int(intno, cpu);
    vd->flags=VECDESC_FL_SHARED;
    if (is_int_ram) vd->flags|=VECDESC_FL_INIRAM;
    portEXIT_CRITICAL(&spinlock);

    return ESP_OK;
}

esp_err_t esp_intr_reserve(int intno, int cpu)
{
    if (intno>31) return ESP_ERR_INVALID_ARG;
    if (cpu>=portNUM_PROCESSORS) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&spinlock);
    vector_desc_t *vd=get_desc_for_int(intno, cpu);
    vd->flags=VECDESC_FL_RESERVED;
    portEXIT_CRITICAL(&spinlock);

    return ESP_OK;
}

//Interrupt handler table and unhandled uinterrupt routine. Duplicated 
//from xtensa_intr.c... it's supposed to be private, but we need to look 
//into it in order to see if someone allocated an int using 
//xt_set_interrupt_handler.
typedef struct xt_handler_table_entry {
    void * handler;
    void * arg;
} xt_handler_table_entry;
extern xt_handler_table_entry _xt_interrupt_table[XCHAL_NUM_INTERRUPTS*portNUM_PROCESSORS];
extern void xt_unhandled_interrupt(void * arg);

//Returns true if handler for interrupt is not the default unhandled interrupt handler
static bool int_has_handler(int intr, int cpu) 
{
    return (_xt_interrupt_table[intr*portNUM_PROCESSORS+cpu].handler != xt_unhandled_interrupt);
}


//Locate a free interrupt compatible with the flags given.
//The 'force' argument can be -1, or 0-31 to force checking a certain interrupt.
static int get_free_int(int flags, int cpu, int force)
{
    int x;
    int best=-1;
    int bestLevel=9;
    int bestSharedCt=INT_MAX;
    //Default vector desc, for vectors not in the linked list
    vector_desc_t empty_vect_desc;
    memset(&empty_vect_desc, 0, sizeof(vector_desc_t));
    //Level defaults to any low/med interrupt
    if (!(flags&ESP_INTR_FLAG_LEVELMASK)) flags|=ESP_INTR_FLAG_LOWMED;

    ALCHLOG(TAG, "get_free_int: start looking. Current cpu: %d", cpu);
    //Iterate over the 32 possible interrupts
    for (x=0; x!=31; x++) {
        //Grab the vector_desc for this vector.
        vector_desc_t *vd=find_desc_for_int(x, cpu);
        if (vd==NULL) vd=&empty_vect_desc;
        //See if we have a forced interrupt; if so, bail out if this is not it.
        if (force!=-1 && force!=x) {
            ALCHLOG(TAG, "Ignoring int %d: forced to %d", x, force);
            continue;
        }
        ALCHLOG(TAG, "Int %d reserved %d level %d %s hasIsr %d", 
            x, int_desc[x].cpuflags[cpu]==INTDESC_RESVD, int_desc[x].level, 
            int_desc[x].type==INTTP_LEVEL?"LEVEL":"EDGE", int_has_handler(x, cpu));
        //Check if interrupt is not reserved by design
        if (int_desc[x].cpuflags[cpu]==INTDESC_RESVD) { //ToDo: Check for SPECIAL and force!=-1
            ALCHLOG(TAG, "....Unusable: reserved");
            continue;
        }
        //Check if the interrupt level is acceptable
        if (!(flags&(1<<int_desc[x].level))) {
            ALCHLOG(TAG, "....Unusable: incompatible level");
            continue;
        }
        //check if edge/level type matches what we want
        if (((flags&ESP_INTR_FLAG_EDGE) && (int_desc[x].type==INTTP_LEVEL)) || 
                (((!(flags&ESP_INTR_FLAG_EDGE)) && (int_desc[x].type==INTTP_EDGE)))) {
            ALCHLOG(TAG, "....Unusable: incompatible trigger type");
            continue;
        }
        //Check if interrupt already is allocated by xt_set_interrupt_handler
        if (int_has_handler(x, cpu) && !(vd->flags&VECDESC_FL_SHARED))  {
            ALCHLOG(TAG, "....Unusable: already allocated");
            continue;
        }
        //Ints can't be both shared and non-shared.
        assert(!((vd->flags&VECDESC_FL_SHARED)&&(vd->flags&VECDESC_FL_NONSHARED)));
        //check if interrupt is reserved at runtime
        if (vd->flags&VECDESC_FL_RESERVED)  {
            ALCHLOG(TAG, "....Unusable: reserved at runtime.");
            continue;
        }
        //check if interrupt already is in use by a non-shared interrupt
        if (vd->flags&VECDESC_FL_NONSHARED) {
            ALCHLOG(TAG, "....Unusable: already in (non-shared) use.");
            continue;
        }
        if (flags&ESP_INTR_FLAG_SHARED) {
            //We're allocating a shared int.
            bool in_iram_flag=((flags&ESP_INTR_FLAG_IRAM)!=0);
            bool desc_in_iram_flag=((vd->flags&VECDESC_FL_INIRAM)!=0);
            //Bail out if int is shared, but iram property doesn't match what we want.
            if ((vd->flags&VECDESC_FL_SHARED) && (desc_in_iram_flag!=in_iram_flag))  {
                ALCHLOG(TAG, "....Unusable: shared but iram prop doesn't match");
                continue;
            }
            //See if int already is used as a shared interrupt.
            if (vd->flags&VECDESC_FL_SHARED) {
                //We can use this already-marked-as-shared interrupt. Count the already attached isrs in order to see
                //how useful it is.
                int no=0;
                shared_vector_desc_t *svdesc=vd->shared_vec_info;
                while (svdesc!=NULL) {
                    no++;
                    svdesc=svdesc->next;
                }
                if (no<bestSharedCt || bestLevel>int_desc[x].level) {
                    //Seems like this shared vector is both okay and has the least amount of ISRs already attached to it.
                    best=x;
                    bestSharedCt=no;
                    bestLevel=int_desc[x].level;
                    ALCHLOG(TAG, "...int %d more usable as a shared int: has %d existing vectors", x, no);
                } else {
                    ALCHLOG(TAG, "...worse than int %d", best);
                }
            } else {
                if (best==-1) {
                    //We haven't found a feasible shared interrupt yet. This one is still free and usable, even if 
                    //not marked as shared.
                    //Remember it in case we don't find any other shared interrupt that qualifies.
                    if (bestLevel>int_desc[x].level) {
                        best=x;
                        bestLevel=int_desc[x].level;
                        ALCHLOG(TAG, "...int %d usable as a new shared int", x);
                    }
                } else {
                    ALCHLOG(TAG, "...already have a shared int");
                }
            }
        } else {
            //We need an unshared IRQ; can't use shared ones; bail out if this is shared.
            if (vd->flags&VECDESC_FL_SHARED) {
                ALCHLOG(TAG, "...Unusable: int is shared, we need non-shared.");
                continue;
            }
            //Seems this interrupt is feasible. Select it and break out of the loop; no need to search further.
            if (bestLevel>int_desc[x].level) {
                best=x;
                bestLevel=int_desc[x].level;
            } else {
                ALCHLOG(TAG, "...worse than int %d", best);
            }
        }
    }
    ALCHLOG(TAG, "get_free_int: using int %d", best);

    //Okay, by now we have looked at all potential interrupts and hopefully have selected the best one in best.
    return best;
}


//Common shared isr handler. Chain-call all ISRs.
static void IRAM_ATTR shared_intr_isr(void *arg) 
{
    vector_desc_t *vd=(vector_desc_t*)arg;
    shared_vector_desc_t *sh_vec=vd->shared_vec_info;
    portENTER_CRITICAL(&spinlock);
    while(sh_vec) {
        if ((sh_vec->statusreg == NULL) || (*sh_vec->statusreg & sh_vec->statusmask)) {
            sh_vec->isr(sh_vec->arg);
            sh_vec=sh_vec->next;
        }
    }
    portEXIT_CRITICAL(&spinlock);
}


//We use ESP_EARLY_LOG* here because this can be called before the scheduler is running.
esp_err_t esp_intr_alloc_intrstatus(int source, int flags, uint32_t intrstatusreg, uint32_t intrstatusmask, intr_handler_t handler, 
                                        void *arg, int_handle_t *ret_handle) 
{
    int force=-1;
    ESP_EARLY_LOGV(TAG, "esp_intr_alloc_intrstatus (cpu %d): checking args", xPortGetCoreID());
    //Shared interrupts should be level-triggered.
    if ((flags&ESP_INTR_FLAG_SHARED) && (flags&ESP_INTR_FLAG_EDGE)) return ESP_ERR_INVALID_ARG;
    //You can't set an handler / arg for a non-C-callable interrupt.
    if ((flags&ESP_INTR_FLAG_HIGH) && (handler)) return ESP_ERR_INVALID_ARG;
    //Shared ints should have handler
    if ((flags&ESP_INTR_FLAG_SHARED) && (!handler)) return ESP_ERR_INVALID_ARG;
    //Only shared interrupts can have status reg / mask
    if (intrstatusreg && (!(flags&ESP_INTR_FLAG_SHARED))) return ESP_ERR_INVALID_ARG;
    //Statusreg should have a mask
    if (intrstatusreg && !intrstatusmask) return ESP_ERR_INVALID_ARG;

    //Default to prio 1 for shared interrupts. Default to prio 1, 2 or 3 for non-shared interrupts.
    if ((flags&ESP_INTR_FLAG_LEVELMASK)==0) {
        if (flags&ESP_INTR_FLAG_SHARED) {
            flags|=ESP_INTR_FLAG_LEVEL1;
        } else {
            flags|=ESP_INTR_FLAG_LOWMED;
        }
    }
    ESP_EARLY_LOGV(TAG, "esp_intr_alloc_intrstatus (cpu %d): Args okay. Resulting flags 0x%X", xPortGetCoreID(), flags);
    
    //Check 'special' interrupt sources. These are tied to one specific interrupt, so we
    //have to force get_free_int to only look at that.
    if (source==ETS_INTERNAL_TIMER0_INTR_SOURCE) force=ETS_INTERNAL_TIMER0_INTR_NO;
    if (source==ETS_INTERNAL_TIMER1_INTR_SOURCE) force=ETS_INTERNAL_TIMER1_INTR_NO;
    if (source==ETS_INTERNAL_TIMER2_INTR_SOURCE) force=ETS_INTERNAL_TIMER2_INTR_NO;
    if (source==ETS_INTERNAL_SW0_INTR_SOURCE) force=ETS_INTERNAL_SW0_INTR_NO;
    if (source==ETS_INTERNAL_SW1_INTR_SOURCE) force=ETS_INTERNAL_SW1_INTR_NO;
    if (source==ETS_INTERNAL_PROFILING_INTR_SOURCE) force=ETS_INTERNAL_PROFILING_INTR_NO;

    portENTER_CRITICAL(&spinlock);
    int cpu=xPortGetCoreID();
    //See if we can find an interrupt that matches the flags.
    int intr=get_free_int(flags, cpu, force);
    if (intr==-1) {
        //None found. Bail out.
        portEXIT_CRITICAL(&spinlock);
        return ESP_ERR_NOT_FOUND;
    }
    //Get an int vector desc for int.
    vector_desc_t *vd=get_desc_for_int(intr, cpu);

    //Allocate that int!
    if (flags&ESP_INTR_FLAG_SHARED) {
        //Populate vector entry and add to linked list.
        shared_vector_desc_t *sh_vec=malloc(sizeof(shared_vector_desc_t));
        memset(sh_vec, 0, sizeof(shared_vector_desc_t));
        sh_vec->statusreg=(uint32_t*)intrstatusreg;
        sh_vec->statusmask=intrstatusmask;
        sh_vec->isr=handler;
        sh_vec->arg=arg;
        sh_vec->next=vd->shared_vec_info;
        vd->shared_vec_info=sh_vec;
        vd->flags|=VECDESC_FL_SHARED;
        //(Re-)set shared isr handler to new value.
        xt_set_interrupt_handler(intr, shared_intr_isr, vd);
    } else {
        //Mark as unusable for other interrupt sources. This is ours now!
        vd->flags=VECDESC_FL_NONSHARED;
        if (handler) {
            xt_set_interrupt_handler(intr, handler, arg);
        }
        if (flags&ESP_INTR_FLAG_EDGE) xthal_set_intclear(1 << intr);
    }
    if (flags&ESP_INTR_FLAG_IRAM) {
        vd->flags|=VECDESC_FL_INIRAM;
        non_iram_int_mask[cpu]&=~(1<<intr);
    } else {
        vd->flags&=~VECDESC_FL_INIRAM;
        non_iram_int_mask[cpu]|=(1<<intr);
    }
    if (source>=0) {
        intr_matrix_set(cpu, source, intr);
    }
    //If we should return a handle, allocate it here.
    if (ret_handle!=NULL) {
        int_handle_data_t *ret;
        ret=malloc(sizeof(int_handle_data_t));
        ret->vector_desc=vd;
        ret->shared_vector_desc=vd->shared_vec_info;
        *ret_handle=ret;
    }

    //We enable the interrupt in any case. For shared interrupts, the interrupts are enabled as soon as we exit 
    //the critical region anyway, so this is consistent.
    portEXIT_CRITICAL(&spinlock);
    ESP_EARLY_LOGD(TAG, "Connected src %d to int %d (cpu %d)", source, intr, cpu);
    ESP_INTR_ENABLE(intr);
    return ESP_OK;
}

esp_err_t esp_intr_alloc(int source, int flags, intr_handler_t handler, void *arg, int_handle_t *ret_handle) 
{
    /*
      As an optimization, we can create a table with the possible interrupt status registers and masks for every single
      source there is. We can then add code here to look up an applicable value and pass that to the 
      esp_intr_alloc_intrstatus function.
    */
    return esp_intr_alloc_intrstatus(source, flags, 0, 0, handler, arg, ret_handle);
}


esp_err_t esp_intr_free(int_handle_t handle) 
{
    bool free_shared_vector=false;
    if (!handle) return ESP_ERR_INVALID_ARG;
    //This routine should be called from the interrupt the task is scheduled on.
    if (to_cpu(handle->vector_desc->intno_cpu)!=xPortGetCoreID()) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&spinlock);
    if (handle->vector_desc->flags&VECDESC_FL_SHARED) {
        //Find and kill the shared int 
        shared_vector_desc_t *svd=handle->vector_desc->shared_vec_info;
        shared_vector_desc_t *prevsvd=NULL;
        assert(svd); //should be something in there for a shared int
        while (svd!=NULL) {
            if (svd==handle->shared_vector_desc) {
                //Found it. Now kill it.
                if (prevsvd) {
                    prevsvd->next=svd->next;
                } else {
                    handle->vector_desc->shared_vec_info=svd->next;
                }
                free(svd);
                break;
            }
            prevsvd=svd;
            svd=svd->next;
        }
        //If nothing left, disable interrupt.
        if (handle->vector_desc->shared_vec_info==NULL) free_shared_vector=true;
        ESP_LOGV(TAG, "esp_intr_free: Deleting shared int: %s. Shared int is %s", svd?"not found or last one":"deleted", free_shared_vector?"empty now.":"still in use");
    }

    if ((handle->vector_desc->flags&VECDESC_FL_NONSHARED) || free_shared_vector) {
        ESP_LOGV(TAG, "esp_intr_free: Disabling int, killing handler");
        //Interrupt is not shared. Just disable it and revert to the default interrupt handler.
        ESP_INTR_DISABLE(to_intno(handle->vector_desc->intno_cpu));
        xt_set_interrupt_handler(to_intno(handle->vector_desc->intno_cpu), xt_unhandled_interrupt, NULL);
        //Theoretically, we could free the vector_desc... not sure if that's worth the few bytes of memory
        //we save.(We can also not use the same exit path for empty shared ints anymore if we delete 
        //the desc.) For now, just mark it as free. 
        handle->vector_desc->flags&=!(VECDESC_FL_NONSHARED|VECDESC_FL_RESERVED);
        //Also kill non_iram mask bit.
        non_iram_int_mask[to_cpu(handle->vector_desc->intno_cpu)]&=~(1<<(to_intno(handle->vector_desc->intno_cpu)));
    }
    portEXIT_CRITICAL(&spinlock);
    free(handle);
    return ESP_OK;
}

int esp_intr_get_intno(int_handle_t handle)
{
    return to_intno(handle->vector_desc->intno_cpu);
}

int esp_intr_get_cpu(int_handle_t handle)
{
    return to_cpu(handle->vector_desc->intno_cpu);
}

esp_err_t esp_intr_enable(int_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle->shared_vector_desc) return ESP_ERR_INVALID_ARG; //Shared ints can't be enabled using this function.
    if (to_cpu(handle->vector_desc->intno_cpu)!=xPortGetCoreID()) return ESP_ERR_INVALID_ARG; //Can only enable ints on this cpu
    ESP_INTR_ENABLE(to_intno(handle->vector_desc->intno_cpu));
    return ESP_OK;
}

esp_err_t esp_intr_disable(int_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle->shared_vector_desc) return ESP_ERR_INVALID_ARG; //Shared ints can't be disabled using this function.
    if (to_cpu(handle->vector_desc->intno_cpu)!=xPortGetCoreID()) return ESP_ERR_INVALID_ARG; //Can only disable ints on this cpu
    ESP_INTR_DISABLE(to_intno(handle->vector_desc->intno_cpu));
    return ESP_OK;
}


void esp_intr_noniram_disable() 
{
    int oldint;
    int cpu=xPortGetCoreID();
    int intmask=~non_iram_int_mask[cpu];
    assert(non_iram_int_disabled_flag[cpu]==false);
    non_iram_int_disabled_flag[cpu]=true;
    asm volatile (
        "movi %0,0\n"
        "xsr %0,INTENABLE\n"    //disable all ints first
        "rsync\n"
        "and a3,%0,%1\n"        //mask ints that need disabling
        "wsr a3,INTENABLE\n"    //write back
        "rsync\n"
        :"=r"(oldint):"r"(intmask):"a3");
    //Save which ints we did disable
    non_iram_int_disabled[cpu]=oldint&non_iram_int_mask[cpu];
}

void esp_intr_noniram_enable() 
{
    int cpu=xPortGetCoreID();
    int intmask=non_iram_int_disabled[cpu];
    assert(non_iram_int_disabled_flag[cpu]==true);
    non_iram_int_disabled_flag[cpu]=false;
    asm volatile (
        "movi a3,0\n"
        "xsr a3,INTENABLE\n"
        "rsync\n"
        "or a3,a3,%0\n"
        "wsr a3,INTENABLE\n"
        "rsync\n"
        ::"r"(intmask):"a3");
}






