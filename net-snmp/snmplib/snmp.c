/*
 * Simple Network Management Protocol (RFC 1067).
 *
 */
/**********************************************************************
	Copyright 1988, 1989, 1991, 1992 by Carnegie Mellon University

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of CMU not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
******************************************************************/

#include <config.h>
#include <ctype.h>

#ifdef KINETICS
#include "gw.h"
#include "ab.h"
#include "inet.h"
#include "fp4/cmdmacro.h"
#include "fp4/pbuf.h"
#include "glob.h"
#endif

#include <stdio.h>

#include <sys/types.h>
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#if HAVE_WINSOCK_H
#include <winsock.h>
#endif
#ifndef NULL
#define NULL 0
#endif

#ifdef vms
#include <in.h>
#endif

#include "asn1.h"
#include "snmp.h"
#include "snmp_api.h"
#include "snmp_impl.h"
#include <stdlib.h>
#include "snmp_logging.h"
#include "mib.h"

void
xdump(const u_char *cp,
      size_t length,
      const char *prefix)
{
    int col, count;
    char *buffer;

    buffer=(char *)malloc(strlen(prefix)+5+4*13+2+16+1);
    if (!buffer) {
      snmp_log(LOG_NOTICE, "xdump: malloc failed. packet-dump skipped\n");
      return;
    }

    count = 0;
    while(count < (int)length){
	strcpy(buffer, prefix);

	for(col = 0; ((count + col) < (int)length) && col < 16; col++){
	    if (col == 0) sprintf (buffer+strlen(buffer), "%.4d: ", count);
	    else if (col % 4 == 0) strcat(buffer, " "); 
	    sprintf(buffer+strlen(buffer), "%02X ", cp[count + col]);
	}
	while(col++ < 16){	/* pad end of buffer with zeros */
	    if (col % 4 == 0)
		strcat(buffer, " ");
	    strcat(buffer, "   ");
	}
	strcat(buffer, "  ");
	for(col = 0;
	  ((count + col) < (int)length) && col < 16;
	  col++){
	    if (isprint(cp[count + col]))
		sprintf(buffer+strlen(buffer), "%c", cp[count + col]);
	    else
		strcat(buffer, ".");
	}
	strcat(buffer, "\n");
        snmp_log(LOG_DEBUG, buffer);
	count += col;
    }
    free(buffer);

}  /* end xdump() */

/* 
   u_char * snmp_parse_var_op(
   u_char *data              IN - pointer to the start of object
   oid *var_name	     OUT - object id of variable 
   int *var_name_len         IN/OUT - length of variable name 
   u_char *var_val_type      OUT - type of variable (int or octet string) (one byte) 
   int *var_val_len          OUT - length of variable 
   u_char **var_val	     OUT - pointer to ASN1 encoded value of variable 
   int *listlength          IN/OUT - number of valid bytes left in var_op_list 
*/

u_char *
snmp_parse_var_op(u_char *data,
		  oid *var_name,
		  size_t *var_name_len,
		  u_char *var_val_type,
		  size_t *var_val_len,
		  u_char **var_val,
		  size_t *listlength)
{
    return( snmp_dparse_var_op(data, var_name, var_name_len,
			var_val_type, var_val_len, var_val, listlength, PARSE_PACKET));
}
u_char *
snmp_dparse_var_op(u_char *data,
		  oid *var_name,
		  size_t *var_name_len,
		  u_char *var_val_type,
		  size_t *var_val_len,
		  u_char **var_val,
		  size_t *listlength,
		  int action)
{
    u_char	    var_op_type;
    size_t		    var_op_len = *listlength;
    u_char	    *var_op_start = data;

    data = asn_dparse_header(data, &var_op_len, &var_op_type, action);
    if (data == NULL){
	ERROR_MSG("No header for variable");
	return NULL;
    }
    if (var_op_type != (u_char)(ASN_SEQUENCE | ASN_CONSTRUCTOR))
	return NULL;
    data = asn_dparse_objid(data, &var_op_len, &var_op_type, var_name, var_name_len, action);
    if (data == NULL){
	ERROR_MSG("No OID for variable");
	return NULL;
    }
    if (var_op_type != (u_char)(ASN_UNIVERSAL | ASN_PRIMITIVE | ASN_OBJECT_ID))
	return NULL;
    *var_val = data;	/* save pointer to this object */
    /* find out what type of object this is */
    data = asn_dparse_header(data, &var_op_len, var_val_type, PARSE_PACKET);
    if (data == NULL){
	ERROR_MSG("No header for value");
	return NULL;
    }
    *var_val_len = var_op_len;
    data += var_op_len;
    *listlength -= (int)(data - var_op_start);
    return data;
}

/*
        u_char * snmp_build_var_op(
	u_char *data	     IN - pointer to the beginning of the output buffer
	oid *var_name        IN - object id of variable 
	int *var_name_len    IN - length of object id 
	u_char var_val_type  IN - type of variable 
	int    var_val_len   IN - length of variable 
	u_char *var_val      IN - value of variable 
	int *listlength      IN/OUT - number of valid bytes left in
				   output buffer 
*/

u_char *
snmp_build_var_op(u_char *data,
		  oid *var_name,
		  size_t *var_name_len,
		  u_char var_val_type,
		  size_t var_val_len,
		  u_char *var_val,
		  size_t *listlength)
{
    size_t     dummyLen, headerLen;
    u_char    *dataPtr;

    dummyLen = *listlength;
    dataPtr = data;
#if 0
    data = asn_build_sequence(data, &dummyLen,
			      (u_char)(ASN_SEQUENCE | ASN_CONSTRUCTOR), 0);
    if (data == NULL){
	ERROR_MSG("Can't build sequence header");
	return NULL;
    }
#endif
    data += 4;
    dummyLen -=4;
    if (dummyLen < 0)
	return NULL;

    headerLen = data - dataPtr;
    *listlength -= headerLen;
    data = asn_build_objid(data, listlength,
	    (u_char)(ASN_UNIVERSAL | ASN_PRIMITIVE | ASN_OBJECT_ID),
	    var_name, *var_name_len);
    if (data == NULL){
	ERROR_MSG("Can't build OID for variable");
	return NULL;
    }
    switch(var_val_type){
	case ASN_INTEGER:
	    data = asn_build_int(data, listlength, var_val_type,
		    (long *)var_val, var_val_len);
	    break;
	case ASN_GAUGE:
	case ASN_COUNTER:
	case ASN_TIMETICKS:
	case ASN_UINTEGER:
	    data = asn_build_unsigned_int(data, listlength, var_val_type,
					  (u_long *)var_val, var_val_len);
	    break;
#ifdef OPAQUE_SPECIAL_TYPES
	case ASN_OPAQUE_COUNTER64:
	case ASN_OPAQUE_U64:
#endif
	case ASN_COUNTER64:
	    data = asn_build_unsigned_int64(data, listlength, var_val_type,
					   (struct counter64 *)var_val,
					    var_val_len);
	    break;
	case ASN_OCTET_STR:
	case ASN_IPADDRESS:
	case ASN_OPAQUE:
        case ASN_NSAP:
	    data = asn_build_string(data, listlength, var_val_type,
		    var_val, var_val_len);
	    break;
	case ASN_OBJECT_ID:
	    data = asn_build_objid(data, listlength, var_val_type,
		    (oid *)var_val, var_val_len / sizeof(oid));
	    break;
	case ASN_NULL:
	    data = asn_build_null(data, listlength, var_val_type);
	    break;
	case ASN_BIT_STR:
	    data = asn_build_bitstring(data, listlength, var_val_type,
		    var_val, var_val_len);
	    break;
	case SNMP_NOSUCHOBJECT:
	case SNMP_NOSUCHINSTANCE:
	case SNMP_ENDOFMIBVIEW:
	    data = asn_build_null(data, listlength, var_val_type);
	    break;
#ifdef OPAQUE_SPECIAL_TYPES
      case ASN_OPAQUE_FLOAT:
        data = asn_build_float(data, listlength, var_val_type,
                               (float *) var_val, var_val_len);
        break;
      case ASN_OPAQUE_DOUBLE:
        data = asn_build_double(data, listlength, var_val_type,
                               (double *) var_val, var_val_len);
        break;
      case ASN_OPAQUE_I64:
        data = asn_build_signed_int64(data, listlength, var_val_type,
                                      (struct counter64 *) var_val,
                                      var_val_len);
        break;
#endif /* OPAQUE_SPECIAL_TYPES */
	default:
	    ERROR_MSG("wrong type");
	    return NULL;
    }
    if (data == NULL){
	ERROR_MSG("Can't build value");
	return NULL;
    }
    dummyLen = (data - dataPtr) - headerLen;

    asn_build_sequence(dataPtr, &dummyLen,
		       (u_char)(ASN_SEQUENCE | ASN_CONSTRUCTOR), dummyLen);
    return data;
}
