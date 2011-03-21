/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/

/**
 * @file svr_recov.c
 *
 * contains functions to save server state and recover
 *
 * Included functions are:
 * svr_recov()
 * svr_save()
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <sys/param.h>
#include "pbs_ifl.h"
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "queue.h"
#include "server.h"
#include "svrfunc.h"
#include "log.h"
#include "pbs_error.h"
#include "resource.h"
#include "utils.h"

#ifndef MAXLINE
#define MAXLINE 1024
#endif

/* Global Data Items: */

extern struct server server;
extern tlist_head svr_queues;
extern attribute_def svr_attr_def[];
extern char     *path_svrdb;
extern char     *path_svrdb_new;
extern char     *path_priv;
extern time_t    time_now;
extern char     *msg_svdbopen;
extern char     *msg_svdbnosv;
extern int       resc_access_perm;


/**
 * Recover server state from server database.
 *
 * @param ps     A pointer to the server state structure.
 * @param mode   This is either SVR_SAVE_QUICK or SVR_SAVE_FULL.
 * @return       Zero on success or -1 on error.
 */

int svr_recov(

  char *svrfile,  /* I */
  int read_only)  /* I */

  {
  static char *id = "svr_recov";
  int i;
  int sdb;

  void recov_acl(attribute *, attribute_def *, char *, char *);

  sdb = open(svrfile, O_RDONLY, 0);

  if (sdb < 0)
    {
    if (errno == ENOENT)
      {
      char tmpLine[LOG_BUF_SIZE];

      snprintf(tmpLine, sizeof(tmpLine), "cannot locate server database '%s' - use 'pbs_server -t create' to create new database if database has not been initialized.",
               svrfile);

      log_err(errno, id, tmpLine);
      }
    else
      {
      log_err(errno, id, msg_svdbopen);
      }

    return(-1);
    }

  /* read in server structure */

  i = read(sdb, (char *) & server.sv_qs, sizeof(struct server_qs));

  if (i != sizeof(struct server_qs))
    {
    if (i < 0)
      log_err(errno, id, "read of serverdb failed");
    else
      log_err(errno, id, "short read of serverdb");

    close(sdb);

    return(-1);
    }

  /* Save the sv_jobidnumber field in case it is set by the attributes. */
  i = server.sv_qs.sv_jobidnumber;

  /* read in server attributes */

  if (recov_attr(
        sdb,
        &server,
        svr_attr_def,
        server.sv_attr,
        SRV_ATR_LAST,
        0,
        !read_only) != 0 ) 
    {
    log_err(errno, id, "error on recovering server attr");

    close(sdb);

    return(-1);
    }

  /* Restore the current job number and make it visible in qmgr print server commnad. */

  if (!read_only)
    {
    server.sv_qs.sv_jobidnumber = i;

    server.sv_attr[SRV_ATR_NextJobNumber].at_val.at_long = i;

    server.sv_attr[SRV_ATR_NextJobNumber].at_flags |= ATR_VFLAG_SET| ATR_VFLAG_MODIFY;
    }

  close(sdb);

  /* recover the server various acls from their own files */

  for (i = 0;i < SRV_ATR_LAST;i++)
    {
    if (server.sv_attr[i].at_type == ATR_TYPE_ACL)
      {
      recov_acl(
        &server.sv_attr[i],
        &svr_attr_def[i],
        PBS_SVRACL,
        svr_attr_def[i].at_name);

      if ((!read_only) && (svr_attr_def[i].at_action != (int (*)())0))
        {
        svr_attr_def[i].at_action(
          &server.sv_attr[i],
          &server,
          ATR_ACTION_RECOV);
        }
      }
    }    /* END for (i) */

  return(0);
  }  /* END svr_recov() */



int write_buffer(

  char *buf,
  int   len,
  int   fds)

  {
  char *id = "write_buffer";
  int   written;

  while ((written = write(fds,buf,len)) != len)
    {
    if ((errno == EINTR) &&
        (written == -1))
      continue;

    log_err(errno,id,msg_svdbnosv);

    return(-1);
    }

  return(0);
  } /* END write_buffer */





int size_to_str(

  struct size_value  szv,
  char              *out,
  int                space)

  {
  snprintf(out,space,"%lu",szv.atsv_num);
  
  if (space - strlen(out) < 3)
    return(-1);

  /* SUCCESS */
  
  switch (szv.atsv_shift)
    {
    case 10:
      
      strcat(out,"kb");
      
      break;
      
    case 20:
      
      strcat(out,"mb");
      
      break;
      
    case 30:
      
      strcat(out,"gb");
      
      break;
      
    case 40:
      
      strcat(out,"tb");
      
      break;
      
    case 50:
      
      strcat(out,"pb");
    }

  return(0);
  } /* END size_to_str */





/*
 * attr_to_str
 *
 * @param out - the string output
 * @param size - the largest possible string length
 * @param aindex - the attribute's index
 * @param attr - the attribute
 */
int attr_to_str(

  char             *out,    /* O */
  int               size,   /* I */
  attribute_def    *at_def, /* I */
  struct attribute  attr,   /* I */
  int               XML)    /* I */

  {
  int rc;

  if ((attr.at_flags & ATR_VFLAG_SET) == FALSE)
    return(NO_ATTR_DATA);

  switch (at_def->at_type)
    {
    case ATR_TYPE_LONG:

      snprintf(out,size,"%ld",attr.at_val.at_long);

      break;

    case ATR_TYPE_CHAR:

      if (size > 1)
        {
        out[0] = attr.at_val.at_char;
        out[1] = '\0';
        }
      else
        {
        return(NO_BUFFER_SPACE);
        }

      break;

    case ATR_TYPE_STR:

      if (attr.at_val.at_str == NULL)
        return(NO_ATTR_DATA);

      if (strlen(attr.at_val.at_str) == 0)
        return(NO_ATTR_DATA);

      if (XML)
        {
        rc = escape_xml(attr.at_val.at_str,out,size);
        if(rc == BUFFER_OVERFLOW)
          {
          return(NO_BUFFER_SPACE);
          }
        }
      else
        snprintf(out,size,"%s",attr.at_val.at_str);

      break;

    case ATR_TYPE_ARST:
    case ATR_TYPE_ACL:

      {
      int j;
      struct array_strings *arst = attr.at_val.at_arst;

      if (arst == NULL)
        return(NO_ATTR_DATA);

      if (size < arst->as_bufsize)
        return(NO_BUFFER_SPACE);

      out[0] = '\0';

      /* concatenate all of the array strings into one string */
      for (j = 0; j < arst->as_usedptr; j++)
        {
        if (j > 0)
          {
          int len;
          strcat(out,",");
          if (XML)
            {
            len = strlen(out);
            rc = escape_xml(arst->as_string[j],out+len,size-len);
            if(rc == BUFFER_OVERFLOW)
              {
              return(NO_BUFFER_SPACE);
              }
            }
          else
            strcat(out,arst->as_string[j]);
          }
        else
          {
          if (XML) 
            {
            int len = strlen(out);
            rc = escape_xml(arst->as_string[j],out+len,size-len);
            if(rc == BUFFER_OVERFLOW)
              {
              return(NO_BUFFER_SPACE);
              }
            }
          else
            strcat(out,arst->as_string[j]);
          }
        }
      }

      break;

    case ATR_TYPE_SIZE:

      size_to_str(attr.at_val.at_size,out,size);

      break;

    case ATR_TYPE_RESC:

      {
      char *ptr;

      int   lspace;
      int   len;

      resource *current = (resource *)GET_NEXT(attr.at_val.at_list);

      if (current == NULL)
        return(NO_ATTR_DATA);

      ptr = out;
      lspace = size;

      /* print all of the resources */
      while (current != NULL)
        {

        /* there are only 3 resource types used */
        switch (current->rs_value.at_type)
          {
          case ATR_TYPE_LONG:

            snprintf(ptr,lspace,"\t\t<%s>%ld</%s>\n",
              current->rs_defin->rs_name,
              current->rs_value.at_val.at_long,
              current->rs_defin->rs_name);
            
            len = strlen(ptr);
            ptr    += len;
            lspace -= len;

            break;

          case ATR_TYPE_STR:

            /* Patch provided by Martin Siegert to fix seg-fault
             * when current->rs_value.at_val.at_str is NULL 
             * Bugzilla bug 101 
             */

            if(current->rs_value.at_val.at_str == NULL)
              break;

            if(strlen(current->rs_value.at_val.at_str) == 0)
              break;

            snprintf(ptr,lspace,"\t\t<%s>",
              current->rs_defin->rs_name);
            len = strlen(ptr);
            ptr    += len;
            lspace -= len;

            if (XML)
              rc = escape_xml(current->rs_value.at_val.at_str,ptr,lspace);
            if(rc == BUFFER_OVERFLOW)
              {
              return(NO_BUFFER_SPACE);
              }
            else
              snprintf(ptr,lspace,"%s",current->rs_value.at_val.at_str);

            len = strlen(ptr);
            ptr += len;
            lspace -= len;

            snprintf(ptr,lspace,"</%s>\n",
              current->rs_defin->rs_name);
            len = strlen(ptr);
            ptr += len;
            lspace -= len;

            break;

          case ATR_TYPE_SIZE:

            snprintf(ptr,lspace,"\t\t<%s>",current->rs_defin->rs_name);
            len = strlen(ptr);
            ptr += len;
            lspace -= len;

            size_to_str(current->rs_value.at_val.at_size,ptr,size);
            len = strlen(ptr);
            ptr += len;
            lspace -= len;

            snprintf(ptr,lspace,"</%s>\n",current->rs_defin->rs_name);
            len = strlen(ptr);
            ptr += len;
            lspace -= len;

            break;
          }

        if (lspace == 0)
          return(NO_BUFFER_SPACE);

        current = (resource *)GET_NEXT(current->rs_link);
        }

      }

      break;

    /* NYI */
    case ATR_TYPE_LIST:
    case ATR_TYPE_LL:
    case ATR_TYPE_SHORT:
    case ATR_TYPE_JINFOP:

      break;
    } /* END switch attribute type */

  return(0);
  } /* END attr_to_str */




/* converts a string to an attribute 
 * @return PBSE_NONE on success
 */

int str_to_attr(

  char                *name,    /* I */
  char                 *val,    /* I */
  struct attribute     *attr,   /* O */
  struct attribute_def *padef)  /* I */

  {
  int   index;
  char *id = "str_to_attr";
  char  buf[MAXLINE<<5];

  if ((name == NULL) ||
      (val  == NULL) ||
      (attr == NULL))
    {
    log_err(-1,id,"Illegal NULL pointer argument");

    return(-10);
    }

  index = find_attr(padef,name,SRV_ATR_LAST);

  if (index < 0)
    {
    /* couldn't find attribute */
    snprintf(log_buffer,sizeof(log_buffer),
      "Couldn't find attribute %s\n",
      name);
    log_err(-1,id,log_buffer);

    return(ATTR_NOT_FOUND);
    }

  switch (padef[index].at_type)
    {
    case ATR_TYPE_LONG:

      attr[index].at_val.at_long = atol(val);

      break;

    case ATR_TYPE_CHAR:

      attr[index].at_val.at_char = *val;

      break;

    case ATR_TYPE_STR:

      unescape_xml(val,buf,sizeof(buf));

      attr[index].at_val.at_str = (char *)malloc(strlen(buf)+1);

      if (attr[index].at_val.at_str == NULL)
        {
        log_err(PBSE_SYSTEM,id,"Cannot allocate memory\n");

        return(PBSE_SYSTEM);
        }

      strcpy(attr[index].at_val.at_str,buf);

      break;

    case ATR_TYPE_ARST:
    case ATR_TYPE_ACL:

      {
      int   rc;

      unescape_xml(val,buf,sizeof(buf));

      if ((rc = decode_arst(attr + index,name,NULL,buf)))
        return(rc);
      }

      break;

    case ATR_TYPE_SIZE:

      {
      unsigned long number;

      char *unit;

      number = atol(val);

      attr[index].at_val.at_size.atsv_units = ATR_SV_BYTESZ;
      attr[index].at_val.at_size.atsv_num = number;
      attr[index].at_val.at_size.atsv_shift = 0;

      /* the string always ends with kb,mb if it has units */
      unit = val + strlen(val) - 2;

      if (unit < val)
        break;
      else if (isdigit(*val))
        break;

      switch (*unit)
        {
        case 'k':

          attr[index].at_val.at_size.atsv_shift = 10;

          break;

        case 'm':

          attr[index].at_val.at_size.atsv_shift = 20;

          break;

        case 'g':

          attr[index].at_val.at_size.atsv_shift = 30;

          break;

        case 't':

          attr[index].at_val.at_size.atsv_shift = 40;

          break;

        case 'p':

          attr[index].at_val.at_size.atsv_shift = 50;

          break;

        }
      }

      break;

    case ATR_TYPE_RESC:

      {
      char *resc_parent;
      char *resc_child;
      char *resc_ptr = val;

      int   len = strlen(resc_ptr);
      int   rc;
      int   errFlg = 0;

      while (resc_ptr - val < len)
        {
        if (get_parent_and_child(resc_ptr,&resc_parent,&resc_child,
              &resc_ptr))
          {
          errFlg = TRUE;

          break;
          }
        
        if ((rc = decode_resc(&(attr[index]),name,resc_parent,resc_child)))
          {
          snprintf(log_buffer,sizeof(log_buffer),
            "Error decoding resource %s, %s = %s\n",
            name,
            resc_parent,
            resc_child);
          
          errFlg = TRUE;

          log_err(rc,id,log_buffer);
          }
        }

      if (errFlg == TRUE)
        return(-1);

      }

      break;

    /* NYI */
    case ATR_TYPE_LIST:
    case ATR_TYPE_LL:
    case ATR_TYPE_SHORT:
    case ATR_TYPE_JINFOP:

      break;
    } /* END switch (attribute type) */

  attr[index].at_flags |= ATR_VFLAG_SET;

  return(PBSE_NONE);
  } /* END str_to_attr */




int svr_recov_xml(

  char *svrfile,  /* I */
  int read_only)  /* I */

  {
  int   sdb;
  int   bytes_read;
  int   errorCount = 0;
  int   rc;

  char  buffer[MAXLINE<<10];
  char *parent;
  char *child;

  char *current;
  char *begin;
  char *end;

  char *id = "svr_recov_xml";

  sdb = open(svrfile, O_RDONLY, 0);

  if (sdb < 0)
    {
    if (errno == ENOENT)
      {
      snprintf(log_buffer,sizeof(log_buffer),
        "cannot locate server database '%s' - use 'pbs_server -t create' to create new database if database has not been initialized.",
        svrfile);

      log_err(errno, id, log_buffer);
      }
    else
      {
      log_err(errno, id, msg_svdbopen);
      }

    return(-1);
    }

  bytes_read = read(sdb,buffer,sizeof(buffer));

  if (bytes_read < 0)
    {
    snprintf(log_buffer,sizeof(log_buffer),
      "Unable to read from serverdb file - %s",
      strerror(errno));

    log_err(errno,id,log_buffer);

    return(-1);
    }

  /* start reading the serverdb file */
  current = begin = buffer;

  /* advance past the server tag */
  current = strstr(current,"<server_db>");
  if (current == NULL)
    {
    /* no server tag - check if this is the old format */
    log_event(PBSEVENT_SYSTEM,
      PBS_EVENTCLASS_SERVER,
      id,
      "Cannot find a server tag, attempting to load legacy format\n");

    close(sdb);
    rc = svr_recov(svrfile,read_only);

    return(rc);
    }
  end = strstr(current,"</server_db>");

  if (end == NULL)
    {
    /* no server tag???? */
    log_err(-1,id,"No server tag found in the database file???");

    return(-1);
    }

  /* adjust to not process server tag */
  current += strlen("<server_db>");
  /* adjust end for the newline character preceeding the close server tag */
  end--;

  while (current < end)
    {
    if (get_parent_and_child(current,&parent,&child,&current))
      {
      /* ERROR */
      errorCount++;

      break;
      }

    if (!strcmp("numjobs",parent))
      {
      server.sv_qs.sv_numjobs = atoi(child);
      }
    else if (!strcmp("numque",parent))
      {
      server.sv_qs.sv_numque = atoi(child);
      }
    else if (!strcmp("nextjobid",parent))
      {
      server.sv_qs.sv_jobidnumber = atoi(child);
      }
    else if (!strcmp("savetime",parent))
      {
      server.sv_qs.sv_savetm = atol(child);
      }
    else if (!strcmp("attributes",parent))
      {
      char *attr_ptr = child;
      char *child_parent;
      char *child_attr;
      
      resc_access_perm = ATR_DFLAG_ACCESS;

      while (*attr_ptr != '\0')
        {
        if (get_parent_and_child(attr_ptr,&child_parent,&child_attr,
              &attr_ptr))
          {
          /* ERROR */
          errorCount++;

          break;
          }

        if ((rc = str_to_attr(child_parent,child_attr,server.sv_attr,svr_attr_def)))
          {
          /* ERROR */
          errorCount++;
          snprintf(log_buffer,sizeof(log_buffer),
            "Error creating attribute %s",
            child_parent);

          log_err(rc,id,log_buffer);

          break;
          }
        }
      }
    else
      {
      /* shouldn't get here */
      }
    }
    
  if (!read_only)
    {
    server.sv_attr[SRV_ATR_NextJobNumber].at_val.at_long = 
      server.sv_qs.sv_jobidnumber;
    
    server.sv_attr[SRV_ATR_NextJobNumber].at_flags |= 
      ATR_VFLAG_SET| ATR_VFLAG_MODIFY;
    }

  return(0);
  }
   







int svr_save_xml(

  struct server *ps,
  int            mode)

  {
  char *id = "svr_save_xml";
  char  buf[MAXLINE<<8];

  int   fds;
  int   rc;
  int   len;

  fds = open(path_svrdb, O_WRONLY | O_CREAT | O_Sync | O_TRUNC, 0600);

  if (fds < 0)
    {
    log_err(errno,id,msg_svdbopen);

    return(-1);
    }

  /* write the sv_qs info */
  snprintf(buf,sizeof(buf),
    "<server_db>\n<numjobs>%d</numjobs>\n<numque>%d</numque>\n<nextjobid>%d</nextjobid>\n<savetime>%ld</savetime>\n",
    ps->sv_qs.sv_numjobs,
    ps->sv_qs.sv_numque,
    ps->sv_qs.sv_jobidnumber,
    time_now);
  len = strlen(buf);

  if ((rc = write_buffer(buf,len,fds)))
    return(rc);

  if ((rc = save_attr_xml(svr_attr_def,ps->sv_attr,SRV_ATR_LAST,fds)) != 0)
    return(rc);

  /* close the server_db */
  snprintf(buf,sizeof(buf),"</server_db>");
  if ((rc = write_buffer(buf,strlen(buf),fds)))
    return(rc);

  close(fds);

  return(0);
  } /* END svr_save_xml */





/**
 * Save the state of the server (server structure).
 *
 * @param ps     A pointer to the server state structure.
 * @param mode   This is either SVR_SAVE_QUICK or SVR_SAVE_FULL.
 * @return       Zero on success or -1 on error.
 */

int svr_save(

  struct server *ps,
  int            mode)

  {
  return(svr_save_xml(ps,mode));
  }  /* END svr_save() */




/**
 * Save an Access Control List to its file.
 *
 * @param attr   A pointer to an acl (access control list) attribute.
 * @param pdef   A pointer to the attribute definition structure.
 * @param subdir The sub-directory path specifying where to write the file.
 * @param name   The parent object name which in this context becomes the file name.
 * @return       Zero on success (File may not be written if attribute is clean) or -1 on error.
 */

int save_acl(

  attribute     *attr,  /* acl attribute */
  attribute_def *pdef,  /* attribute def structure */
  char          *subdir, /* sub-directory path */
  char          *name)  /* parent object name = file name */

  {
  static char *this_function_name = "save_acl";
  int  fds;
  char filename1[MAXPATHLEN];
  char filename2[MAXPATHLEN];
  tlist_head    head;
  int      i;
  svrattrl    *pentry;

  if ((attr->at_flags & ATR_VFLAG_MODIFY) == 0)
    {
    return(0);   /* Not modified, don't bother */
    }

  attr->at_flags &= ~ATR_VFLAG_MODIFY;

  strcpy(filename1, path_priv);
  strcat(filename1, subdir);
  strcat(filename1, "/");
  strcat(filename1, name);

  if ((attr->at_flags & ATR_VFLAG_SET) == 0)
    {
    /* has been unset, delete the file */

    unlink(filename1);

    return(0);
    }

  strcpy(filename2, filename1);

  strcat(filename2, ".new");

  fds = open(filename2, O_WRONLY | O_CREAT | O_TRUNC | O_Sync, 0600);

  if (fds < 0)
    {
    snprintf(log_buffer, 1024, "unable to open acl file '%s'",
             filename2);

    log_err(errno, this_function_name, log_buffer);

    return(-1);
    }

  CLEAR_HEAD(head);

  i = pdef->at_encode(attr, &head, pdef->at_name, (char *)0, ATR_ENCODE_SAVE);

  if (i < 0)
    {
    log_err(-1, this_function_name, "unable to encode acl");

    close(fds);

    unlink(filename2);

    return(-1);
    }

  pentry = (svrattrl *)GET_NEXT(head);

  if (pentry != NULL)
    {
    /* write entry, but without terminating null */

    while ((i = write(fds, pentry->al_value, pentry->al_valln - 1)) != pentry->al_valln - 1)
      {
      if ((i == -1) && (errno == EINTR))
        continue;

      log_err(errno, this_function_name, "wrote incorrect amount");

      close(fds);

      unlink(filename2);

      return(-1);
      }

    free(pentry);
    }

  close(fds);

  unlink(filename1);

  if (link(filename2, filename1) < 0)
    {
    log_err(errno, this_function_name, "unable to relink file");

    return(-1);
    }

  unlink(filename2);

  attr->at_flags &= ~ATR_VFLAG_MODIFY; /* clear modified flag */

  return(0);
  }




/**
 * Reload an Access Control List from its file.
 *
 * @param attr   A pointer to an acl (access control list) attribute.
 * @param pdef   A pointer to the attribute definition structure.
 * @param subdir The sub-directory path specifying where to read the file.
 * @param name   The parent object name which in this context is the file name.
 * @return       Zero on success (File may not be written if attribute is clean) or -1 on error.
 */

void recov_acl(

  attribute     *pattr, /* acl attribute */
  attribute_def *pdef, /* attribute def structure */
  char          *subdir, /* directory path */
  char         *name) /* parent object name = file name */

  {
  static char *this_function_name = "recov_acl";
  char *buf;
  int   fds;
  char  filename1[MAXPATHLEN];

  struct stat sb;
  attribute tempat;

  errno = 0;

  strcpy(filename1, path_priv);

  if (subdir != NULL)
    {
    strcat(filename1, subdir);
    strcat(filename1, "/");
    }

  strcat(filename1, name);

  fds = open(filename1, O_RDONLY, 0600);

  if (fds < 0)
    {
    if (errno != ENOENT)
      {
      sprintf(log_buffer, "unable to open acl file %s",
              filename1);

      log_err(errno, this_function_name, log_buffer);
      }

    return;
    }

  if (fstat(fds, &sb) < 0)
    {
    close(fds);

    return;
    }

  if (sb.st_size == 0)
    {
    close(fds);

    return;  /* no data */
    }

  buf = malloc((size_t)sb.st_size + 1); /* 1 extra for added null */

  if (buf == NULL)
    {
    close(fds);

    return;
    }

  if (read(fds, buf, (unsigned int)sb.st_size) != (int)sb.st_size)
    {
    log_err(errno, this_function_name, "unable to read acl file");

    close(fds);
    free(buf);

    return;
    }

  close(fds);

  *(buf + sb.st_size) = '\0';

  clear_attr(&tempat, pdef);

  if (pdef->at_decode(&tempat, pdef->at_name, NULL, buf) < 0)
    {
    sprintf(log_buffer, "decode of acl %s failed",
            pdef->at_name);

    log_err(errno, this_function_name, log_buffer);
    }
  else if (pdef->at_set(pattr, &tempat, SET) != 0)
    {
    sprintf(log_buffer, "set of acl %s failed",
            pdef->at_name);

    log_err(errno, this_function_name, log_buffer);
    }

  pdef->at_free(&tempat);

  free(buf);

  return;
  }  /* END recov_acl() */

/* END svr_recov.c  */

