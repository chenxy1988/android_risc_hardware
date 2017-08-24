#ifndef __QL__CONF_H__
#define __QL__CONF_H__

typedef struct {
    int LTE_SignalStrength;
    int LTE_Is_Report_SignalStrength;
    int Icc_Constants[64];
    char At_Cmds_For_Customer[128][64];
} RIL_CONF_INIT;

RIL_CONF_INIT s_ril_conf_init;

int read_conf_of_ril_init();


#endif

