#include "license_pbs.h" /* See here for the software license */

#include "attribute.h" /* attribute */
#include "list_link.h" /* tlist_head */
#include "pbs_ifl.h" /* batch_op */

int decode_c( attribute *patr, char *name, char *rescn, char *val, int perm); 

int encode_c( attribute *attr, tlist_head *phead, char *atname, char *rsname, int mode, int perm); 

int set_c(struct attribute *attr, struct attribute *new, enum batch_op op);

int comp_c(struct attribute *attr, struct attribute *with);
