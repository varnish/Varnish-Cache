/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vcc_gen_obj.tcl instead
 */

struct sockaddr * VRT_r_client_ip(const struct sess *);
struct sockaddr * VRT_r_server_ip(struct sess *);
int VRT_r_server_port(struct sess *);
const char * VRT_r_req_request(const struct sess *);
void VRT_l_req_request(const struct sess *, const char *, ...);
const char * VRT_r_req_url(const struct sess *);
void VRT_l_req_url(const struct sess *, const char *, ...);
const char * VRT_r_req_proto(const struct sess *);
void VRT_l_req_proto(const struct sess *, const char *, ...);
void VRT_l_req_hash(struct sess *, const char *);
struct director * VRT_r_req_backend(struct sess *);
void VRT_l_req_backend(struct sess *, struct director *);
int VRT_r_req_restarts(const struct sess *);
double VRT_r_req_grace(struct sess *);
void VRT_l_req_grace(struct sess *, double);
const char * VRT_r_req_xid(struct sess *);
const char * VRT_r_bereq_request(const struct sess *);
void VRT_l_bereq_request(const struct sess *, const char *, ...);
const char * VRT_r_bereq_url(const struct sess *);
void VRT_l_bereq_url(const struct sess *, const char *, ...);
const char * VRT_r_bereq_proto(const struct sess *);
void VRT_l_bereq_proto(const struct sess *, const char *, ...);
const char * VRT_r_obj_proto(const struct sess *);
void VRT_l_obj_proto(const struct sess *, const char *, ...);
int VRT_r_obj_status(const struct sess *);
void VRT_l_obj_status(const struct sess *, int);
const char * VRT_r_obj_response(const struct sess *);
void VRT_l_obj_response(const struct sess *, const char *, ...);
int VRT_r_obj_hits(const struct sess *);
unsigned VRT_r_obj_cacheable(const struct sess *);
void VRT_l_obj_cacheable(const struct sess *, unsigned);
double VRT_r_obj_ttl(const struct sess *);
void VRT_l_obj_ttl(const struct sess *, double);
double VRT_r_obj_grace(const struct sess *);
void VRT_l_obj_grace(const struct sess *, double);
double VRT_r_obj_prefetch(const struct sess *);
void VRT_l_obj_prefetch(const struct sess *, double);
double VRT_r_obj_lastuse(const struct sess *);
const char * VRT_r_obj_hash(const struct sess *);
const char * VRT_r_resp_proto(const struct sess *);
void VRT_l_resp_proto(const struct sess *, const char *, ...);
int VRT_r_resp_status(const struct sess *);
void VRT_l_resp_status(const struct sess *, int);
const char * VRT_r_resp_response(const struct sess *);
void VRT_l_resp_response(const struct sess *, const char *, ...);
double VRT_r_now(const struct sess *);
unsigned VRT_r_req_backend_healthy(const struct sess *);
