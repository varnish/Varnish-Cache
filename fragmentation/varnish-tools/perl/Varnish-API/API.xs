#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include <varnish/varnishapi.h>

#include "const-c.inc"

int
dispatch_callback(void *priv, enum shmlogtag tag, unsigned fd, unsigned len,
    unsigned spec, const char *ptr) {
    dSP;
    int count;
    int rv;

    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSVpv(VSL_tags[tag],0)));
    XPUSHs(sv_2mortal(newSViv(fd)));
    XPUSHs(sv_2mortal(newSViv(spec)));
    XPUSHs(sv_2mortal(newSVpv(ptr,0)));
    PUTBACK;
    count = call_sv((SV*) priv, G_SCALAR);
    SPAGAIN;
    rv = POPi;
    PUTBACK;    
    FREETMPS;
    LEAVE;
    return (rv);
}


SV*
get_field_type() {
		 dTHX;
	     HV* fields;
	     fields = newHV();
#define MAC_STAT(type, l, field, description)	\
	{ \
	  char * tmp = #field; \
	  hv_store(fields, #type, strlen(#type), newSVpv(&tmp[1], 1),0); \
		}\
		
#include <varnish/stat_field.h>
#undef MAC_STAT
       return newRV_noinc((SV*) fields);
}

SV*
get_field_descriptions() {
		 dTHX;
	     HV* fields;
	     fields = newHV();
#define MAC_STAT(type, l, field, description)	\
	  hv_store(fields, #type, strlen(#type), newSVpv(description, 0),0); \
		
#include <varnish/stat_field.h>
#undef MAC_STAT
       return newRV_noinc((SV*) fields);
}

SV*
get_tags() {
		 dTHX;
	     HV* fields;
	     fields = newHV();
	     int i = 1;
#define SLTM(foo)       \
	  hv_store(fields, #foo, strlen(#foo), newSViv(i++),0); \

#include <varnish/shmlog_tags.h>		
#undef SLTM
       return newRV_noinc((SV*) fields);
}

IV
get_stat(struct varnish_stats *VSL_stats, const char* stat) {


#define MAC_STAT(type, l, field, description)	\
  if(!strcmp(#type, stat)) {			\
    return VSL_stats->type;			\
  }				   \
			   
#include <varnish/stat_field.h>
#undef MAC_STAT

}


MODULE = Varnish::API		PACKAGE = Varnish::API		


INCLUDE: const-xs.inc

SV*
VSL_GetStatFieldTypes()
	CODE:
	RETVAL = get_field_type();
	OUTPUT:
	RETVAL

SV*
VSL_GetTags()
	CODE:
	RETVAL = get_tags();
	OUTPUT:
	RETVAL


SV*
VSL_GetStatFieldDescriptions()
	CODE:
	RETVAL = get_field_descriptions();
	OUTPUT:
	RETVAL
	

unsigned int
SHMLOG_ID(logentry)
	SV* logentry;
	CODE:
	RETVAL = SHMLOG_ID((unsigned char*) SvPVbyte_nolen(logentry));
	OUTPUT:
	RETVAL

unsigned int
SHMLOG_LEN(logentry)
	SV* logentry
	CODE:
	RETVAL = SHMLOG_LEN((unsigned char*) SvPVbyte_nolen(logentry));
	OUTPUT:
	RETVAL

unsigned int
SHMLOG_TAG(logentry)
	unsigned char* logentry
	CODE:
	enum shmlogtag tag;
	tag = logentry[SHMLOG_TAG];
	RETVAL = tag;
	OUTPUT:
	RETVAL



char*
SHMLOG_DATA(logentry)
	unsigned char* logentry
	CODE:
	RETVAL = logentry + SHMLOG_DATA;
	OUTPUT:
	RETVAL

char*
VSL_tags(tag)
	int tag
	CODE:
	RETVAL = (char*) VSL_tags[tag];
	OUTPUT:
	RETVAL

int
VSL_Dispatch(vd, func)
	SV* vd;
	SV* func
	PPCODE:
	struct VSL_data* data = (struct VSL_data*) SvIV(vd);
	VSL_Dispatch(data, dispatch_callback, func);
	

const char *
VSL_Name()

SV*
VSL_New()
	PPCODE:
	struct VSL_data* vd = VSL_New();
	ST(0) = newSViv((IV)vd);
	sv_2mortal(ST(0));
	XSRETURN(1);

SV* 
VSL_NextLog(vd)
	SV* vd;
	PPCODE:
	STRLEN strlen;
	struct VSL_data* data = (struct VSL_data*) SvIV(vd);
	unsigned char *p;
	VSL_NextLog(data, &p);
	ST(0) = newSVpv(p, SHMLOG_DATA + SHMLOG_LEN(p));
	sv_2mortal(ST(0));
	XSRETURN(1);
	
void
VSL_NonBlocking(vd, nb)
        SV* vd
	int	nb
	CODE:
	struct VSL_data* data = (struct VSL_data*) SvIV(vd);
	VSL_NonBlocking(data, nb);

int
VSL_OpenLog(vd, varnish_name)
	SV*	vd
	const char *	varnish_name
	CODE:
	struct VSL_data* data = (struct VSL_data*) SvIV(vd);
	VSL_OpenLog(data, varnish_name);


SV*
VSL_OpenStats(varnish_name)
	const char *	varnish_name
	PPCODE:
	struct varnish_stats *stats = VSL_OpenStats(varnish_name);
	ST(0) = newSViv((IV)stats);
	sv_2mortal(ST(0));
	XSRETURN(1);

SV*
VSL_GetStat(sd, stat)
	SV* sd
	const char * stat
	CODE:
	struct varnish_stats * stats = (struct varnish_stats *)SvIV(sd);
	IV nr = get_stat(stats, stat);
	RETVAL = newSViv(nr);
	OUTPUT:
	RETVAL

int
varnish_instance(n_arg, name, namelen, dir, dirlen)
	const char *	n_arg
	char *	name
	size_t	namelen
	char *	dir
	size_t	dirlen
