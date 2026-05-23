#include"tdfu_prv.h"
// #include"cfg.h"


#if(SC_FILE_FORMAT_KFW)
#define Retrieval_Statu "KCFWID"

#if(DUAL_IMAGE_HDR)
#define ANOTHER_Retrieval_Statu "ABabJDFW"
#endif

#else
#define Retrieval_Statu "ABabJDFW"

#if(DUAL_IMAGE_HDR)
#define ANOTHER_Retrieval_Statu "KCFWID"
#endif

#endif

typedef struct 
{
    unsigned char RetrievalStatu[sizeof(Retrieval_Statu)-1];
	unsigned char type;
    unsigned char MODEL_len;
    unsigned char MODEL[sizeof(MY_FWU_STRING_NAME)-1];
   
	unsigned char type1;
    unsigned char FIRMWARE_len;
    unsigned char FIRMWARE[sizeof(MY_FW_VERSION)-1];
	unsigned char end;
} BINHEXVERSION;

static BINHEXVERSION Version_Retrieval =
{
    Retrieval_Statu,
	0x01,	//id device name 
 	sizeof(MY_FWU_STRING_NAME)-1,
    MY_FWU_STRING_NAME,
	0x02,	//id firmversion 
 	sizeof(MY_FW_VERSION)-1,
 	MY_FW_VERSION,
	0x00,	//id end
};

unsigned char *my_dfu_get_prv_data(void)
{
	return (unsigned char*)&Version_Retrieval;
}

unsigned char my_dfu_get_prv_len(void)
{
	return sizeof(Version_Retrieval);
}

#if(DUAL_IMAGE_HDR)

typedef struct 
{
    unsigned char RetrievalStatu[sizeof(ANOTHER_Retrieval_Statu)-1];
	unsigned char type;
    unsigned char MODEL_len;
    unsigned char MODEL[sizeof(MY_FWU_STRING_NAME)-1];
   
	unsigned char type1;
    unsigned char FIRMWARE_len;
    unsigned char FIRMWARE[sizeof(MY_FW_VERSION)-1];
	unsigned char end;
} BINHEXVERSION_FIXES;

static BINHEXVERSION_FIXES fixes_Version_Retrieval =
{
    ANOTHER_Retrieval_Statu,
	0x01,	//id device name 
 	sizeof(MY_FWU_STRING_NAME)-1,
    MY_FWU_STRING_NAME,
	0x02,	//id firmversion 
 	sizeof(MY_FW_VERSION)-1,
 	MY_FW_VERSION,
	0x00,	//id end
};

unsigned char *my_dfu_get_prv_data_fixes(void)
{
	return (unsigned char*)&fixes_Version_Retrieval;
}

unsigned char my_dfu_get_prv_len_fixes(void)
{
	return sizeof(fixes_Version_Retrieval);
}

#endif

