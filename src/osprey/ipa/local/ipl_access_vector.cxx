/*
 * Copyright 2004, 2005, 2006 PathScale, Inc.  All Rights Reserved.
 */

/*

  Copyright (C) 2000, 2001 Silicon Graphics, Inc.  All Rights Reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it would be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

  Further, this software is distributed without any warranty that it is
  free of the rightful claim of any third person regarding infringement 
  or the like.  Any license provided herein, whether implied or 
  otherwise, applies only to this software file.  Patent licenses, if 
  any, provided herein do not apply to combinations of this program with 
  other software, or any other product whatsoever.  

  You should have received a copy of the GNU General Public License along
  with this program; if not, write the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston MA 02111-1307, USA.

  Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pky,
  Mountain View, CA 94043, or:

  http://www.sgi.com

  For further information regarding this notice, see:

  http://oss.sgi.com/projects/GenInfo/NoticeExplan

*/


//-*-c++-*-

// ====================================================================
// ====================================================================
//
// Module: access_vector.cxx
// $Revision: 1.8 $
// $Date: 04/12/29 20:05:39-08:00 $
// $Author: kannann@iridot.keyresearch $
//
// Revision history:
//  dd-mmm-94 - Original Version
//
// Description:		Access Vectors
//
// This is the basic data structure used to represent array
// expressions. It allows us to succintly represent the locations
// accessed by array instructions, in terms of loop and symbolic
// variables.
//
// ====================================================================
// ====================================================================

/** DAVID CODE BEGIN **/

#ifdef USE_PCH
#include "lno_pch.h"
#endif // USE_PCH
#pragma hdrstop

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <sys/types.h>
#include <alloca.h>
#ifdef LNO
#include "lnopt_main.h"
#else
#include "mempool.h"
static MEM_POOL LNO_local_pool;
#endif
#include "mat.h"
#include "ipl_access_vector.h"
#include "stab.h"
/** DAVID CODE BEGIN **/
#include "ipl_lwn_util.h"
/*** DAVID CODE END ***/
#include "opt_du.h"
#include "soe.h"
#ifdef LNO
#include "lnoutils.h"
#include "move.h"
#include "errors.h"
#include "erbe.h"
#endif
#include "targ_sim.h"


#ifndef LNO
static DU_MANAGER *Du_Mgr;
extern void Initialize_Access_Vals(DU_MANAGER* du_mgr, FILE *tfile);
extern void Finalize_Access_Vals();
extern WN* LNO_Common_Loop(WN* wn1, WN* wn2);
extern WN* UBvar (WN *end);	/* In lieu of lnoutils.h for IPL */
template <> MEM_POOL* MAT<mINT32>::_default_pool = &LNO_local_pool;
// since wopt.so is loaded dynamically in ipl
#pragma weak Add_Def_Use__10DU_MANAGERGP2WNT1
#endif

#ifdef LNO
extern BOOL Is_Consistent_Condition(IPL_ACCESS_VECTOR*, WN*);
#endif


// The following functions are used in place of snprintf(), which belongs 
// to a new version of <stdio.h>, but is not available in the version of 
// <stdio.h> which is available in this compiler. 

INT snprintfs(char* buf, 
	      INT ccount, 
              INT tcount, 
	      const char* fstring)
{
  INT len = strlen(fstring);
  if (ccount + len < tcount) {
    INT new_char_count = sprintf(buf + ccount, fstring);
    return ccount + new_char_count;
  } 
  for (INT i = 0; i < ccount; i++)
    sprintf(buf + i, "%c", '&');
  sprintf(buf + ccount, "%c", '\0');
  return tcount - 1;
} 

INT snprintfd(char* buf, 
	      INT ccount, 
              INT tcount, 
	      INT32 value)
{
  // A 32 bit integer can be expressed in 10 digits plus sign
  const INT int_size = 11;
  if (ccount + int_size < tcount) { 
    INT new_char_count = sprintf(buf + ccount, "%d", value);
    return ccount + new_char_count;
  } 
  for (INT i = 0; i < ccount; i++)
    sprintf(buf + i, "%c", '&');
  sprintf(buf + ccount, "%c", '\0');
  return tcount - 1;
} 

INT snprintfll(char* buf, 
	       INT ccount, 
	       INT tcount, 
	       INT64 value)
{ 
  // A 64 bit integer can be expressed in 20 digits plus sign
  const INT ll_size = 21;
  if (ccount + ll_size < tcount) {
    INT new_char_count = sprintf(buf + ccount, "%lld", value);
    return ccount + new_char_count;
  } 
  for (INT i = 0; i < ccount; i++)
    sprintf(buf + i, "%c", '&');
  sprintf(buf + ccount, "%c", '\0');
  return tcount - 1;
} 

INT snprintfx(char* buf, 
	      INT ccount, 
	      INT tcount, 
	      INT32 value)
{ 
  // A 32 bit integer can be expressed in 8 hexdigits plus "0x"
  const INT x_size = 10;
  if (ccount + x_size < tcount) {
    INT new_char_count = sprintf(buf + ccount, "0x%x", value);
    return ccount + new_char_count;
  } 
  for (INT i = 0; i < ccount; i++)
    sprintf(buf + i, "%c", '&');
  sprintf(buf + ccount, "%c", '\0');
  return tcount - 1;
} 

INT Num_Lands(WN *wn);
INT Num_Liors(WN *wn);

BOOL LNO_Debug_Delinearization;
BOOL LNO_Allow_Nonlinear = TRUE;

#define MAX_NAME_SIZE 66

INT Num_Lower_Bounds(WN* wn, 
			    IPL_ACCESS_VECTOR* step)
{ 
  INT num_bounds=0;
  WN *l = WN_start(wn);
  WN *kid = WN_kid(l,0);
  if (step->Const_Offset > 0) {
    if (WN_operator(kid) == OPR_MAX) { 
      num_bounds = Num_Maxs(kid);
    } else if (WN_operator(kid) == OPR_SUB) { 
      num_bounds = Num_Maxs(WN_kid0(kid));
    } else if (WN_operator(kid) == OPR_ADD) { 
      num_bounds = Num_Maxs(WN_kid0(kid));
      if (!num_bounds) num_bounds = Num_Maxs(WN_kid1(kid));
    }
  } else {
    if (WN_operator(kid) == OPR_MIN) { 
      num_bounds = Num_Mins(kid);
    } else if (WN_operator(kid) == OPR_SUB) { 
      num_bounds = Num_Mins(WN_kid0(kid));
    } else if (WN_operator(kid) == OPR_ADD) { 
      num_bounds = Num_Mins(WN_kid0(kid));
      if (!num_bounds) num_bounds = Num_Maxs(WN_kid1(kid));
    }
  }
  return num_bounds+1;
}

extern INT Num_Upper_Bounds(WN* wn)
{
  INT num_bounds = 0; 
  WN *u = WN_end(wn);
  OPERATOR compare = WN_operator(u);
  if ((compare == OPR_LE) || (compare == OPR_LT)) {
    num_bounds = Num_Mins(WN_kid(u,1));
    if (!num_bounds) num_bounds = Num_Maxs(WN_kid(u,0));
  } else {
    num_bounds = Num_Maxs(WN_kid(u,1));
    if (!num_bounds) num_bounds = Num_Mins(WN_kid(u,0));
  }
  return num_bounds+1; 
}

extern BOOL Bound_Is_Too_Messy(IPL_ACCESS_ARRAY* aa)
{
  if (aa->Too_Messy)
    return TRUE;
  for (INT i = 0; i < aa->Num_Vec(); i++)
    if (aa->Dim(i)->Too_Messy)
      return TRUE;
  return FALSE;
}

#ifdef LNO

extern BOOL Promote_Messy_Bound(WN* wn_loop, WN* wn_bound, char name[],
        DU_MANAGER* du)
{
    if (UBvar(WN_end(wn_loop)) == NULL) return FALSE; 

    WN* wn_parent = LWN_Get_Parent(wn_bound);
    INT i;
    for (i = 0; i < WN_kid_count(wn_parent); i++) 
        if (wn_bound == WN_kid(wn_parent, i)) break;
    FmtAssert(i < WN_kid_count(wn_parent), ("Could not find kid for parent."));

    INT kid = i; 
    TYPE_ID type = WN_desc(WN_start(wn_loop));
    OPCODE preg_s_opcode = OPCODE_make_op(OPR_STID, MTYPE_V, type);
    WN_OFFSET preg_num = Create_Preg(type, name);
    ST* preg_st = MTYPE_To_PREG(type);
/** DAVID CODE BEGIN **/
    WN* wn_stid = IPL_LWN_CreateStid(preg_s_opcode, preg_num, preg_st,
            Be_Type_Tbl(type), wn_bound);
    IPL_LWN_Insert_Block_Before(LWN_Get_Parent(wn_loop), wn_loop, wn_stid);
    WN* wn_ldid = IPL_LWN_CreateLdid(WN_opcode(UBvar(WN_end(wn_loop))), wn_stid);
/*** DAVID CODE END ***/
    WN_kid(wn_parent, kid) = wn_ldid; 
    LWN_Set_Parent(wn_ldid, wn_parent); 
    du->Add_Def_Use(wn_stid, wn_ldid);
    INT hoist_level = Hoistable_Statement(wn_stid, du);
    if (hoist_level < Loop_Depth(wn_stid))
        Hoist_Statement(wn_stid, hoist_level);
    return TRUE; 
}

#endif 

void IPL_INDX_RANGE::Union(INT64 offset, BOOL offset_valid, INT64 mult, INT64 size)
{
  if (Valid) { // a true union
    if (Size == size) {
      if (abs(mult) > abs(Mult)) {
        Mult = mult;
	if (offset_valid) {
	  Min = Max = offset;
	  Min_Max_Valid = TRUE;
        } else {
	  Min_Max_Valid = FALSE;
        }
      } else if (mult == Mult) {
	if (offset_valid && Min_Max_Valid) {
	  Min = MIN(Min,offset);
	  Max = MAX(Max,offset);
        } else if (offset_valid) {
	  Min = Max = offset;
	  Min_Max_Valid = TRUE;
	}
      }
    } else {  // two different sized arrays
	      // chose the smaller
      if (abs((size+mult-1)/mult) < Maxsize()) {
        Mult = mult;
        Size = size;
        if (offset_valid) {
          Min = Max = offset;
          Min_Max_Valid = TRUE;
        } else {
          Min_Max_Valid = FALSE;
        }
      }
    }
  } else { // a set
    Valid = TRUE;
    Mult = mult;
    Size = size;
    if (offset_valid) {
      Min = Max = offset;
      Min_Max_Valid = TRUE;
    } else {
      Min_Max_Valid = FALSE;
    }
  }
}

INT64 IPL_INDX_RANGE::Maxsize() const 
{
  if (!Valid) return -1;
  INT diff = 0;
  if (Min_Max_Valid) {
    diff = Max-Min;
  }
  INT64 c = abs(Mult);
  INT64 ans = (Size - diff + (c-1))/c;
  return ans <= 0 ? -1 : ans;
}



void IPL_ACCESS_ARRAY::Print(FILE *fp, BOOL is_bound) const
{
    if (Too_Messy) {
        fprintf(fp, "Too_Messy\n");
        return;
    }

    for (INT32 i = 0; i < _num_vec; ++i) Dim(i)->Print(fp, is_bound);
    fprintf(fp,"\n");
}

mUINT16 IPL_ACCESS_ARRAY::Non_Const_Loops() const
{
  mUINT16 result = Dim(0)->Non_Const_Loops();
  for (INT32 i=1; i<_num_vec; i++) {
    result = MAX(result, Dim(i)->Non_Const_Loops());
  }
  return result;
}

// set an IPL_ACCESS_ARRAY to be a copy of another
IPL_ACCESS_ARRAY::IPL_ACCESS_ARRAY(const IPL_ACCESS_ARRAY *a, MEM_POOL *pool)
{
  _dim = NULL;
  Init(a,pool);
}

BOOL IPL_ACCESS_ARRAY::Has_Formal_Parameter()
{ 
  if (Too_Messy)
    return FALSE; 
  for (INT i = 0; i< _num_vec; i++) 
    if (Dim(i)->Has_Formal_Parameter())
      return TRUE; 
  return FALSE; 
} 

BOOL IPL_ACCESS_VECTOR::Has_Formal_Parameter()
{
  if (Too_Messy)
    return FALSE; 
  if (Lin_Symb != NULL) { 
    IPL_INTSYMB_ITER ii(Lin_Symb);
    for (IPL_INTSYMB_NODE* nn = ii.First(); !ii.Is_Empty(); nn = ii.Next()) 
      if (nn->Symbol.Is_Formal())
	return TRUE; 
  } 
  if (Non_Lin_Symb != NULL) {
    IPL_SUMPROD_ITER ii(Non_Lin_Symb);
    for (IPL_SUMPROD_NODE* nn = ii.First(); !ii.Is_Empty(); nn = ii.Next()) {
      IPL_SYMBOL_ITER iii(nn->Prod_List);
      for (IPL_SYMBOL_NODE* nnn = iii.First(); !iii.Is_Empty(); nnn = iii.Next()) 
	if (nnn->Symbol.Is_Formal())
	  return TRUE; 
    }
  }
  return FALSE; 
} 

void IPL_ACCESS_VECTOR::Substitute(INT formal_number, 
                               WN* wn_sub,
                               DOLOOP_STACK* stack,
                               BOOL allow_nonlin)
{
  if (Contains_Lin_Symb()) { 
    IPL_INTSYMB_ITER iter(Lin_Symb);
    IPL_INTSYMB_NODE* nnode = NULL;
    IPL_INTSYMB_NODE* node = iter.First();
    for (; !iter.Is_Empty(); nnode = node, node = iter.Next()) {
      if (node->Symbol.Is_Formal() 
	  && node->Symbol.Formal_Number() == formal_number) {
	if (wn_sub == NULL) {
	  Too_Messy = TRUE; 
	  return;
	} 
	OPERATOR opr_sub = WN_operator(wn_sub);
	if (opr_sub != OPR_LDID && opr_sub != OPR_LDA) { 
	  Too_Messy = TRUE; 
	  return; 
	} 
	IPL_SYMBOL sym_sub(WN_st(wn_sub), WN_offset(wn_sub) 
	  + node->Symbol.WN_Offset(), node->Symbol.Type);
        INT32 coeff = node->Coeff;
        Lin_Symb->Remove(nnode, node);
        node = nnode;
        Add_Symbol(coeff, sym_sub, stack, NULL);
        CXX_DELETE(node, _mem_pool);
      }
    }
  }
  if (allow_nonlin && Contains_Non_Lin_Symb()) {
    IPL_SUMPROD_ITER iter(Non_Lin_Symb);
    IPL_SUMPROD_NODE* node = iter.First();
    for (; !iter.Is_Empty(); node = iter.Next()) {
      INT symbol_count = 0;
      IPL_SYMBOL_ITER iiter(node->Prod_List);
      IPL_SYMBOL_NODE* snode = iiter.First();
      for (; !iiter.Is_Empty(); snode = iiter.Next())
        if (snode->Symbol.Is_Formal() 
	    && snode->Symbol.Formal_Number() == formal_number)
          symbol_count++;
      if (symbol_count > 0) { 
	// We are not expecting non-linear terms, since substituitable 
        // formals arise only from LINEXs.  But just in case, go conser-
	// vative.
	DevWarn("IPL_ACCESS_VECTOR::Substitute: Found non-linear term");
	Too_Messy = TRUE; 
	return;  
      } 
    }
  }
}

void IPL_ACCESS_ARRAY::Substitute(INT formal_number, 
                              WN* wn_sub,
                              DOLOOP_STACK* stack,
                              BOOL allow_nonlin)
{
  if (Too_Messy)
    return;
  for (INT i = 0; i < Num_Vec(); i++)
    Dim(i)->Substitute(formal_number, wn_sub, stack, allow_nonlin);
} 

IPL_ACCESS_ARRAY::IPL_ACCESS_ARRAY(
   UINT16 num_vec,IPL_ACCESS_VECTOR* dim[], MEM_POOL *mem_pool=0) 
{
  _dim = CXX_NEW_ARRAY(IPL_ACCESS_VECTOR,num_vec,mem_pool);
  _mem_pool=mem_pool;
  for (INT32 i=0; i<num_vec; i++) {
    _dim[i].Init(dim[i],mem_pool);
  }
  Too_Messy = TRUE;
  _num_vec = num_vec;
}

IPL_ACCESS_ARRAY::IPL_ACCESS_ARRAY(
   UINT16 num_vec,UINT16 nest_depth,MEM_POOL *mem_pool=0) 
{
  _dim = CXX_NEW_ARRAY(IPL_ACCESS_VECTOR,num_vec,mem_pool);
  _mem_pool=mem_pool;
  for (INT32 i=0; i<num_vec; i++) {
    _dim[i].Init(nest_depth,mem_pool);
  }
  Too_Messy = TRUE;
  _num_vec = num_vec;
}


void IPL_ACCESS_ARRAY::Init(const IPL_ACCESS_ARRAY *a, MEM_POOL *pool)
{
  if (_dim != NULL) {
     CXX_DELETE_ARRAY(_dim,_mem_pool);
  }
  _mem_pool = pool;
  Too_Messy = a->Too_Messy;
  _num_vec = a->_num_vec;
  _dim = CXX_NEW_ARRAY(IPL_ACCESS_VECTOR,_num_vec,_mem_pool);

  for (INT32 i=0; i<_num_vec; i++) {
    _dim[i].Init(a->Dim(i),pool);
  }
}

BOOL IPL_ACCESS_ARRAY::operator ==(const IPL_ACCESS_ARRAY& a) const
{
  if (Too_Messy || a.Too_Messy) return (FALSE);
  if (_num_vec != a._num_vec) return(FALSE);
  for (INT32 i=0; i<_num_vec; i++) {
    if (!(*Dim(i) == *a.Dim(i))) {
      return(FALSE);
    }
  }
  return(TRUE);
}

void IPL_ACCESS_VECTOR::Print(FILE *fp, BOOL is_bound, BOOL print_brackets) const
{
  char bf[MAX_TLOG_CHARS]; 
  Print(bf, 0, is_bound, print_brackets);
  fprintf(fp, "%s", bf);
}

INT IPL_ACCESS_VECTOR::Print(char* bf, 
		         INT ccount, 
			 BOOL is_bound, 
			 BOOL print_brackets) const
{
  INT new_ccount = ccount; 
  if (Too_Messy) {
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "[Too_Messy]");
    return new_ccount;
  }
  if (!is_bound && print_brackets) 
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "[");

  // first the loop variable terms
  BOOL seen_something = FALSE;
  if (!is_bound) {
    if (Const_Offset != 0) {
      if (print_brackets) {
        new_ccount = snprintfll(bf, new_ccount, MAX_TLOG_CHARS, Const_Offset);  
	new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, " ");
      } else {
        new_ccount = snprintfll(bf, new_ccount, MAX_TLOG_CHARS, Const_Offset);
      } 
      seen_something = TRUE;
    }
  }
  for (INT32 i = 0; i < Nest_Depth(); i++) {
    if (Loop_Coeff(i) != 0) {
      if (!seen_something) {
        seen_something = TRUE;
	new_ccount = snprintfd(bf, new_ccount, MAX_TLOG_CHARS, Loop_Coeff(i));
	new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "*loop_var");
	new_ccount = snprintfd(bf, new_ccount, MAX_TLOG_CHARS, i);
	new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, " ");
      } else {
	new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "+ ");
	new_ccount = snprintfd(bf, new_ccount, MAX_TLOG_CHARS, Loop_Coeff(i));
	new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "*loop_var");
	new_ccount = snprintfd(bf, new_ccount, MAX_TLOG_CHARS, i);
	new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, " ");
      }
    }
  }
  if (Lin_Symb != NULL && !Lin_Symb->Is_Empty()) {
    if (seen_something) {
      new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "+ ");
    }
    seen_something = TRUE;
    new_ccount = Lin_Symb->Print(bf, new_ccount);
  }
  if (Non_Lin_Symb != NULL && !Non_Lin_Symb->Is_Empty()) {
    new_ccount = Non_Lin_Symb->Print(bf, new_ccount);
  }
  if (!is_bound && (Const_Offset == 0) && !seen_something) {
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "0");
  }
  if (!is_bound) {
    if (print_brackets)
      new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "]");
  } else {
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, " <= ");
    new_ccount = snprintfll(bf, new_ccount, MAX_TLOG_CHARS, Const_Offset);
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, ";  ");
  }
  if (_non_const_loops) {
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, 
      " non_const_loops is ");
    new_ccount = snprintfd(bf, new_ccount, MAX_TLOG_CHARS, _non_const_loops);
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, " \n");
  }
  if (Delinearized_Symbol != NULL) { 
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, 
      " delin_symbol is ");
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, 
      Delinearized_Symbol->Name());
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, " \n");
  } 
  return new_ccount; 
}

void IPL_ACCESS_VECTOR::Print_Analysis_Info(FILE *fp, 
				        DOLOOP_STACK &do_stack, 
				        BOOL is_bound) const
{
  if (Too_Messy) {
    fprintf(fp,"Too_Messy\n");
    return;
  }

  // first the loop variable terms
  BOOL seen_something = FALSE;
  if (!is_bound) {
    if (Const_Offset != 0) {
      fprintf(fp,"%lld ",Const_Offset);
      seen_something = TRUE;
    }
  }

  

  for (INT32 i=0; i< Nest_Depth(); i++) {
    if (Loop_Coeff(i) != 0) {

      if (i >=  do_stack.Elements()) {
	FmtAssert(i < do_stack.Elements(), ("Print_Analysis_Info : loop mismatch"));
      }

      IPL_SYMBOL sym(WN_index(do_stack.Bottom_nth(i)));
      if (!seen_something) {
        seen_something = TRUE;
	fprintf(fp,"%d*%s", Loop_Coeff(i), sym.Name());
      } else {
	fprintf(fp," + %d*%s", Loop_Coeff(i), sym.Name());
      }
    }
  }
  if (Lin_Symb != NULL && !Lin_Symb->Is_Empty()) {
    if (seen_something) {
      fprintf(fp," + ");
    }
    seen_something = TRUE;
    Lin_Symb->Print(fp);
  }
  if (Non_Lin_Symb != NULL && !Non_Lin_Symb->Is_Empty()) {
    Non_Lin_Symb->Print(fp);
  }
  if (!is_bound && (Const_Offset == 0) && !seen_something) {
    fprintf(fp,"0");
  }
  if (is_bound) {
    fprintf(fp," <= %lld;  ",Const_Offset);
  }
  if (_non_const_loops && Lin_Symb && !Lin_Symb->Is_Empty()) {
    fprintf(fp,"non_const_loops is %d \n",_non_const_loops);
  }
}

BOOL IPL_ACCESS_VECTOR::operator ==(const IPL_ACCESS_VECTOR& av) const
{
  if (Too_Messy || av.Too_Messy) return (FALSE);
  if (Const_Offset != av.Const_Offset) return(FALSE);
  if (_non_const_loops != av._non_const_loops) return FALSE;
  if ((Delinearized_Symbol != 0) != (av.Delinearized_Symbol != 0)) {
    return FALSE;
  }
  if (Delinearized_Symbol && 
      (*Delinearized_Symbol != *av.Delinearized_Symbol)) return FALSE;
  INT common_depth = MIN(_nest_depth,av._nest_depth);
  INT32 i;

  for (i=0; i< common_depth; i++) {
    if (Loop_Coeff(i) != av.Loop_Coeff(i)) {
      return FALSE;
    }
  }
  for (i=common_depth; i<_nest_depth; i++) {
    if (Loop_Coeff(i)) {
      return FALSE;
    }
  }
  for (i=common_depth; i<av._nest_depth; i++) {
    if (av.Loop_Coeff(i)) {
      return FALSE;
    }
  }

  if (Lin_Symb != NULL && !Lin_Symb->Is_Empty()) { // this has a lin symb
    if (av.Lin_Symb == NULL || av.Lin_Symb->Is_Empty() ||
	!(*Lin_Symb == *av.Lin_Symb)) {
      return(FALSE);
    }
  } else if (av.Lin_Symb != NULL && !av.Lin_Symb->Is_Empty()) {
    return(FALSE);
  }

  if (Non_Lin_Symb != NULL && !Non_Lin_Symb->Is_Empty()) {
    if (av.Non_Lin_Symb == NULL || av.Non_Lin_Symb->Is_Empty() ||
	!(*Non_Lin_Symb == *av.Non_Lin_Symb)) {
      return(FALSE);
    }
  } else if (av.Non_Lin_Symb != NULL && !av.Non_Lin_Symb->Is_Empty()) {
    return(FALSE);
  }

  return(TRUE);
}


IPL_ACCESS_VECTOR::IPL_ACCESS_VECTOR(const IPL_ACCESS_VECTOR *a, MEM_POOL *pool)
{
    Init(a,pool);
}

void IPL_ACCESS_VECTOR::Init(const IPL_ACCESS_VECTOR *a, MEM_POOL *pool)
{
    _mem_pool = pool;
    _nest_depth = a->_nest_depth;
    _non_const_loops = a->_non_const_loops;
    Delinearized_Symbol = a->Delinearized_Symbol;

    if (a->_lcoeff) {
        _lcoeff = CXX_NEW_ARRAY(mINT32,_nest_depth,_mem_pool);
        for (INT i = 0; i < _nest_depth; i++) _lcoeff[i] = a->_lcoeff[i];
    } else {
        _lcoeff = NULL;
    }

    Too_Messy = a->Too_Messy;
    Const_Offset = a->Const_Offset;

    if (a->Lin_Symb) {
        Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST,_mem_pool);
        Lin_Symb->Init(a->Lin_Symb,_mem_pool);
    } else {
        Lin_Symb = NULL;
    }

    if (a->Non_Lin_Symb) {
        Non_Lin_Symb = CXX_NEW(IPL_SUMPROD_LIST,_mem_pool);
        Non_Lin_Symb->Init(a->Non_Lin_Symb,_mem_pool);
    } else {
        Non_Lin_Symb = NULL;
    }
}

void IPL_ACCESS_VECTOR::Set_Loop_Coeff(UINT16 i, INT32 val)
{
  if (!_lcoeff) {
    _lcoeff = CXX_NEW_ARRAY(mINT32, _nest_depth, _mem_pool);
    for (INT j=0; j < _nest_depth; j++) {
      _lcoeff[j] = 0;
    }
  }
  _lcoeff[i] = val;
}

BOOL IPL_ACCESS_VECTOR::Is_Const() const
{
  if (Too_Messy ||
      (Lin_Symb && !Lin_Symb->Is_Empty()) ||
      (Non_Lin_Symb && !Non_Lin_Symb->Is_Empty())) {
    return(FALSE);
  }
  if (!_lcoeff) return(TRUE);
  for (INT32 i=0; i< _nest_depth; i++) {
    if (_lcoeff[i] != 0) return(FALSE);
  }
  return(TRUE);
}


void IPL_INTSYMB_LIST::Print(FILE *fp) const
{
  char bf[MAX_TLOG_CHARS]; 
  Print(bf, 0);
  fprintf(fp, "%s", bf);
} 

INT IPL_INTSYMB_LIST::Print(char* bf, INT ccount) const
{
  INT new_ccount = ccount; 
  IPL_INTSYMB_CONST_ITER iter(this);
  const IPL_INTSYMB_NODE* first = iter.First();
  for (const IPL_INTSYMB_NODE *node=first; !iter.Is_Empty(); node = iter.Next()) {
      if (node != first) 
        new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "+ ");
      new_ccount = node->Print(bf, new_ccount);
  }
  return new_ccount; 
}

BOOL IPL_INTSYMB_LIST::operator ==(const IPL_INTSYMB_LIST& isl) const
{
  IPL_INTSYMB_CONST_ITER iter(this);
  IPL_INTSYMB_CONST_ITER iter2(&isl);
  const IPL_INTSYMB_NODE *node2 = iter2.First();
  for (const IPL_INTSYMB_NODE *node=iter.First(); !iter.Is_Empty(); ) {
    if (iter2.Is_Empty() || !(*node == *node2)) return(FALSE); 
    node = iter.Next(); 
    node2 = iter2.Next(); 
  }
  if (!iter2.Is_Empty()) return(FALSE);
  return (TRUE);
}

void IPL_INTSYMB_LIST::Init(IPL_INTSYMB_LIST *il, MEM_POOL *mem_pool)
{
    IPL_INTSYMB_ITER iter(il);
    for (IPL_INTSYMB_NODE *in = iter.First(); !iter.Is_Empty(); in=iter.Next()) {
        Append(CXX_NEW(IPL_INTSYMB_NODE(in), mem_pool));
    }
}

// delete a list, including every node on it
// this assumes that all elements are from the same mempool
// and that the default mempool has been set
IPL_INTSYMB_LIST::~IPL_INTSYMB_LIST()
{
  while (!Is_Empty()) CXX_DELETE(Remove_Headnode(),Default_Mem_Pool);
}

// Subtract two IPL_INTSYMB_LISTs 
// This is a n^2 process.  If these lists become big, we might want to
// sort them at some point
IPL_INTSYMB_LIST *Subtract(IPL_INTSYMB_LIST *list1, IPL_INTSYMB_LIST *list2,
				     MEM_POOL *pool)
{
  IPL_INTSYMB_LIST *res = CXX_NEW(IPL_INTSYMB_LIST,pool);
  if (list1) res->Init(list1,pool);  // init result to list1
  if (!list2) return (res);

  IPL_INTSYMB_ITER iter2(list2);
  for (IPL_INTSYMB_NODE *node2 = iter2.First(); !iter2.Is_Empty(); 
	node2 = iter2.Next()) {  // go through every element of list2
    // search for node2 in the result list
    IPL_INTSYMB_ITER iterr(res);
    IPL_INTSYMB_NODE *noder=iterr.First(), *prevnoder=NULL;
    while (!iterr.Is_Empty() && !(noder->Symbol == node2->Symbol)) {
      prevnoder = noder;
      noder = iterr.Next();
    }
    if (iterr.Is_Empty()) { // It's not in the result list
      res->Prepend(CXX_NEW(IPL_INTSYMB_NODE(node2->Symbol,-node2->Coeff),pool));
    } else {
      noder->Coeff -= node2->Coeff;
      if (noder->Coeff == 0) { // get rid of it
	if (noder == iterr.First()) {
	  CXX_DELETE(res->Remove_Headnode(),pool);
	} else {
	  CXX_DELETE(res->Remove(prevnoder,noder),pool);
	}
      }
    }
  }
  if (res->Is_Empty()) return (NULL);
  return(res);
}

// Add two IPL_INTSYMB_LISTs 
// This is a n^2 process.  If these lists become big, we might want to
// sort them at some point
IPL_INTSYMB_LIST *Add(IPL_INTSYMB_LIST *list1, IPL_INTSYMB_LIST *list2,
				     MEM_POOL *pool)
{
  IPL_INTSYMB_LIST *res = CXX_NEW(IPL_INTSYMB_LIST,pool);
  if (list1) res->Init(list1,pool);  // init result to list1
  if (!list2) return (res);

  IPL_INTSYMB_ITER iter2(list2);
  for (IPL_INTSYMB_NODE *node2 = iter2.First(); !iter2.Is_Empty(); 
	node2 = iter2.Next()) {  // go through every element of list2
    // serach for node2 in the result list
    IPL_INTSYMB_ITER iterr(res);
    IPL_INTSYMB_NODE *noder=iterr.First(), *prevnoder=NULL;
    while (!iterr.Is_Empty() && !(noder->Symbol == node2->Symbol)) {
      prevnoder = noder;
      noder = iterr.Next();
    }
    if (iterr.Is_Empty()) { // It's not in the result list
      res->Prepend(CXX_NEW(IPL_INTSYMB_NODE(node2->Symbol,node2->Coeff),pool));
    } else {
      noder->Coeff += node2->Coeff;
      if (noder->Coeff == 0) { // get rid of it
	if (noder == iterr.First()) {
	  CXX_DELETE(res->Remove_Headnode(),pool);
	} else {
	  CXX_DELETE(res->Remove(prevnoder,noder),pool);
	}
      }
    }
  }
  if (res->Is_Empty()) return (NULL);
  return(res);
}

// Mul and IPL_INTSYMB_LIST and an integer constant.

IPL_INTSYMB_LIST *Mul(INT c, IPL_INTSYMB_LIST *list, MEM_POOL *pool)
{
  if (list == NULL || c == 0)
    return NULL;

  IPL_INTSYMB_LIST *res = CXX_NEW(IPL_INTSYMB_LIST,pool);
  res->Init(list,pool);

  IPL_INTSYMB_ITER iter(res);
  for (IPL_INTSYMB_NODE* node = iter.First(); !iter.Is_Empty(); node = iter.Next())
    node->Coeff *= c;

  return(res);
}


void IPL_INTSYMB_NODE::Print(FILE *fp) const
{
  char bf[MAX_TLOG_CHARS]; 
  Print(bf, 0);
  fprintf(fp, "%s", bf);
}

INT IPL_INTSYMB_NODE::Print(char* bf, INT ccount) const
{
  INT new_ccount = ccount; 
  new_ccount = snprintfd(bf, new_ccount, MAX_TLOG_CHARS, Coeff);
  new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "*");
  new_ccount = Symbol.Print(bf, new_ccount);
  return new_ccount; 
}

void IPL_SUMPROD_LIST::Print(FILE *fp) const
{
  char bf[MAX_TLOG_CHARS]; 
  Print(bf, 0);
  fprintf(fp, "%s", bf);
} 

INT IPL_SUMPROD_LIST::Print(char* bf, INT ccount) const
{
  INT new_ccount = ccount;
  IPL_SUMPROD_CONST_ITER iter(this);
  for (const IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty(); 
	node = iter.Next()) {
      new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "+ ");
      new_ccount = node->Print(bf, new_ccount);
  }
  return new_ccount;
} 

INT IPL_SUMPROD_LIST::Negate_Me() 
{
  IPL_SUMPROD_ITER iter(this);
  for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty(); 
	node=iter.Next()) {
    INT64 coeff = -node->Coeff;
    if ((coeff >= (INT32_MAX-1)) || (coeff<=(INT32_MIN+1))) {
      return 0;
    }
    node->Coeff = coeff;
  }
  return 1;
}

void IPL_SUMPROD_LIST::Merge(IPL_SUMPROD_LIST *sl)
{
  while (!sl->Is_Empty()) Append(sl->Remove_Headnode());
}
  

BOOL IPL_SUMPROD_LIST::operator ==(const IPL_SUMPROD_LIST& sl) const
{
  IPL_SUMPROD_CONST_ITER iter(this);
  IPL_SUMPROD_CONST_ITER iter2(&sl);
  const IPL_SUMPROD_NODE *node2 = iter2.First();
  for (const IPL_SUMPROD_NODE *node=iter.First(); !iter.Is_Empty(); ) {
    if (iter2.Is_Empty() || !(*node == *node2)) return(FALSE); 
    node = iter.Next(); 
    node2 = iter2.Next(); 
  }
  if (!iter2.Is_Empty()) return(FALSE);
  return (TRUE);
}


void IPL_SUMPROD_LIST::Init(IPL_SUMPROD_LIST *sp,MEM_POOL *mem_pool)
{
  IPL_SUMPROD_ITER iter(sp);
  for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty(); 
	node=iter.Next()) {
	Append(CXX_NEW(IPL_SUMPROD_NODE(node,mem_pool),mem_pool));
  }
}

IPL_SUMPROD_LIST::~IPL_SUMPROD_LIST()
{
  while (!Is_Empty()) CXX_DELETE(Remove_Headnode(),Default_Mem_Pool);
}

void IPL_SUMPROD_NODE::Print(FILE *fp) const
{
  char bf[MAX_TLOG_CHARS]; 
  Print(bf, 0);
  fprintf(fp, "%s", bf);
}

INT IPL_SUMPROD_NODE::Print(char* bf, INT ccount) const
{
  INT new_ccount = ccount; 
  new_ccount = snprintfd(bf, new_ccount, MAX_TLOG_CHARS, Coeff);
  new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "*");
  new_ccount = Prod_List->Print(bf, new_ccount, TRUE);
  return new_ccount; 
}

void IPL_SYMBOL_LIST::Print(FILE *fp, BOOL starsep) const
{
  char bf[MAX_TLOG_CHARS]; 
  Print(bf, 0, starsep);
  fprintf(fp, "%s", bf);
} 

INT IPL_SYMBOL_LIST::Print(char* bf, INT ccount, BOOL starsep) const
{
  INT new_ccount = ccount; 
  IPL_SYMBOL_CONST_ITER iter(this);
  for (const IPL_SYMBOL_NODE *node = iter.First(); !iter.Is_Empty(); 
	node=iter.Next()) {
    new_ccount = node->Print(bf, new_ccount);
    if (iter.Peek_Next() != NULL) {
      new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, 
        starsep ? "*":",");
    } 
  }
  return new_ccount; 
} 

BOOL IPL_SYMBOL_LIST::Contains(const IPL_SYMBOL *s) 
{
  IPL_SYMBOL_CONST_ITER iter(this);
  for (const IPL_SYMBOL_NODE *node = iter.First(); !iter.Is_Empty(); 
	node=iter.Next()) {
    if (node->Symbol == *s) return TRUE;
  }
  return FALSE;
}

BOOL IPL_SYMBOL_LIST::operator ==(const IPL_SYMBOL_LIST& sl) const
{
  IPL_SYMBOL_CONST_ITER iter(this);
  IPL_SYMBOL_CONST_ITER iter2(&sl);
  const IPL_SYMBOL_NODE *node2 = iter2.First();
  for (const IPL_SYMBOL_NODE *node=iter.First(); !iter.Is_Empty(); ) {
    if (iter2.Is_Empty() || !(*node == *node2)) return(FALSE); 
    node = iter.Next(); 
    node2 = iter2.Next(); 
  }
  if (!iter2.Is_Empty()) return(FALSE);
  return (TRUE);
}

void IPL_SYMBOL_LIST::Init(const IPL_SYMBOL_LIST *sl,MEM_POOL *mem_pool)
{
  IPL_SYMBOL_CONST_ITER iter(sl);
  for (const IPL_SYMBOL_NODE *node = iter.First(); !iter.Is_Empty(); 
	node=iter.Next()) {
	Append(CXX_NEW(IPL_SYMBOL_NODE(node),mem_pool));
  }
}

IPL_SYMBOL_LIST::~IPL_SYMBOL_LIST()
{
  while (!Is_Empty()) CXX_DELETE(Remove_Headnode(),Default_Mem_Pool);
}

void IPL_SYMBOL_NODE::Print(FILE *fp) const
{
  char bf[MAX_TLOG_CHARS]; 
  Print(bf, 0);
  fprintf(fp, "%s", bf);
}

INT IPL_SYMBOL_NODE::Print(char* bf, INT ccount) const
{
  INT new_ccount = ccount; 
  new_ccount = Symbol.Print(bf, new_ccount);
  return new_ccount; 
}

void IPL_SYMBOL::Print(FILE *fp) const
{
  char bf[MAX_TLOG_CHARS]; 
  Print(bf, 0);
  fprintf(fp, "%s", bf);
}

INT IPL_SYMBOL::Print(char* bf, INT ccount) const
{
  // call IPL_SYMBOL::Name() to keep all symbol printing code in one place.
  // It would be faster to not do the extra copy into the buf (by duplicating
  // the printing code in this routine), but this is safer, and printing
  // to a file is already slow enough that who cares about an extra copy.

  INT new_ccount = ccount; 
  if (_is_formal) { 
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, "#");
    new_ccount = snprintfd(bf, new_ccount, MAX_TLOG_CHARS, u._formal_number);
  } else {  
    const INT bufsz = 256;
    char buf[bufsz]; 
    (void) Name(buf, bufsz);
    new_ccount = snprintfs(bf, new_ccount, MAX_TLOG_CHARS, buf);
  } 
  return new_ccount; 
}

char* IPL_SYMBOL::Name() const
{
  const INT   bufsz = 128;
  static char name[bufsz];
  if (_is_formal) { 
    sprintf(name, "#%d", u._formal_number);
    return name; 
  } else { 
    return Name(name, bufsz);
  } 
}

static INT Num_Chars_Needed(INT number)
{
  INT value = number; 
  if (value == 0)
    return 2;
  INT count = 1;
  if (value < 0) {
    value = -value; 
    count++; 
  }
  while (value > 0) {
    value /= 10; 
    count++; 
  } 
  return count; 
}  

char* IPL_SYMBOL::Name(char* buf, INT bufsz) const
{
  if (buf == NULL) {
    DevWarn("IPL_SYMBOL::Name(buf, bufsz) shouldn't be called with buf == NULL");
    return Name();
  }
  if (bufsz < 1) {
    DevWarn("IPL_SYMBOL::Name(buf, bufsz) shouldn't be called with bufsz < 1");
    return NULL; 
  } 
  if (_is_formal) {
    INT chars = Num_Chars_Needed(u._formal_number);
    char* goalname = (char*) alloca(chars + 1);
    sprintf(goalname, "#%d", u._formal_number);
    if (bufsz < chars + 1) {
      return NULL;
    } else {
      strcpy(buf, goalname);
      return buf;  
    } 
  } 

  char*     goalname;
  INT       max_woff_len = 3*sizeof(WN_OFFSET);

  // step 1: allocate space for entire name, put it in goalname.

  if (u._st == NULL) {
    goalname = (char*) alloca(max_woff_len + strlen("$null_st") + 2);
    sprintf(goalname, "$null_st.%d", WN_Offset());
  }
  else if (ST_class(u._st) == CLASS_PREG) {
    const char* name;
    BOOL        use_woff = TRUE;
    if (WN_Offset() > Last_Dedicated_Preg_Offset) {
      name = Preg_Name(WN_Offset());
      if (name == NULL || name[0] == '\0')
        name = "$preg.noname";
      else if (strcmp(name,"<preg>") == 0)
	name = "preg";
      else
        use_woff = FALSE;
    }
    else
      name = "$preg.dedicated";
    goalname = (char*) alloca(max_woff_len + strlen(name) + 2);
    if (use_woff)
      sprintf(goalname, "%s%d", name, WN_Offset());
    else
      sprintf(goalname, "%s", name);
  }
  else {
    BOOL slow = ST_Offset() != 0 || WN_Offset() != 0;
    char* true_name = ST_name(u._st);

    char* name = 0;
    if (ST_Base()) {
      name = ST_name(ST_Base());
      if (name == NULL || name[0] == '\0') {
        name = (char*) alloca(strlen("$noname0x") + 10); 
        sprintf(name, "$noname0x%p", ST_Base());
      }
    }
    else
      name = "$nobase";

    goalname = 
      (char*) alloca(strlen(name) + strlen(true_name) + 30 + max_woff_len);
    if (slow || ST_Base() != u._st)
      sprintf(goalname, "%s(%s.%lld.%d)", true_name, 
	name, ST_Offset(), WN_Offset());
    else
      sprintf(goalname, "%s", name);
  }

  // truncate into buf.

  if (strlen(goalname) < bufsz)
    strcpy(buf, goalname);
  else {
    strncpy(buf, goalname, bufsz-1);
    buf[bufsz-1] = '\0';
    DevWarn("Symbol name %s shortened to %s", goalname, buf);
  }

  return buf;
}

char* IPL_SYMBOL::Prompf_Name() const
{
  const INT name_sz = 128; 
  static char buf[name_sz]; 
  const char* name = NULL;
  INT i;

  if (_is_formal) {
    sprintf(buf, "#%d", u._formal_number);
    return buf;
  } 

  if (u._st == NULL) {
    name = "<NULL IPL_SYMBOL>"; 
  } else if (ST_class(u._st) == CLASS_PREG) {
    if (WN_Offset() > Last_Dedicated_Preg_Offset) 
      name = Preg_Name(WN_Offset());
    else
      name = "<DEDICATED PREG>";
  } else {
    name = ST_name(St());
  }

  for (i = 0; i < name_sz - 1 && name[i] != '\0'; i++)
    buf[i] = name[i]; 
  buf[i] = '\0';
  return buf; 
}

// All the routines to build access arrays

// Find the line number associated with this references
#ifdef LNO
static SRCPOS Find_Line(WN *wn)
{
    WN *tmp_wn = wn;
    while (OPCODE_is_expression(WN_opcode(tmp_wn))) {
        tmp_wn = LWN_Get_Parent(tmp_wn);
    }
    return WN_Get_Linenum(tmp_wn);
}
#endif


/*****************************************************************************
 *
 * Given an array statement and a stack of all the enclosing DO_LOOPs, build
 * the access array. No projection is performed.
 *
 ****************************************************************************/

void IPL_ACCESS_ARRAY::Set_Array(WN *wn, DOLOOP_STACK *stack)
{
/** DAVID CODE BEGIN **/
    // printf("Inside Set_Array: Du_Mgr = %p, &Du_Mgr = %p\n", Du_Mgr, &Du_Mgr);
/*** DAVID CODE END ***/
    INT32 i;

    Is_True(WN_operator(wn) == OPR_ARRAY,
            ("IPL_ACCESS_ARRAY::Set_Array called on a non-array"));
    Is_True(_num_vec == WN_num_dim(wn),
            ("IPL_ACCESS_ARRAY::Set_Array called with"
             " an inconsistent number of dimensions"));

    Too_Messy = FALSE;

    // Set up each dimension's subscript.
    for (i = 0; i < _num_vec; i++) {
        _dim[i].Set(WN_array_index(wn,i), stack, 1, 0, LNO_Allow_Nonlinear);
    }

#ifdef LNO
    if (LNO_Allow_Delinearize) Delinearize(stack, wn);
#endif 

    // If any non-linear terms left, update non-const loops.
    for (i = 0; i < _num_vec; i++) {
        if (_dim[i].Contains_Non_Lin_Symb()) {
            _dim[i].Update_Non_Const_Loops_Nonlinear(stack);
        }
    }

    // look at the base, if it varies, update non const loops.
    WN *base = WN_array_base(wn);
    if (WN_operator(base) == OPR_LDID) {
        Update_Non_Const_Loops(base, stack);
#ifdef KEY // Bug 5057 - tolerate base addresses of the form (const + LDID).
    } else if (WN_operator(base) == OPR_ADD &&
            (WN_operator(WN_kid0(base)) == OPR_INTCONST &&
             WN_operator(WN_kid1(base)) == OPR_LDID)) {
        Update_Non_Const_Loops(WN_kid1(base), stack);
    } else if (WN_operator(base) == OPR_ADD &&
            (WN_operator(WN_kid1(base)) == OPR_INTCONST &&
             WN_operator(WN_kid0(base)) == OPR_LDID)) {
        Update_Non_Const_Loops(WN_kid0(base), stack);
#endif
    } else if (WN_operator(base) != OPR_LDA) {
        for (i = 0; i < _num_vec; i++) {
            Dim(i)->Max_Non_Const_Loops(stack->Elements());
        }
    }

    // look at the array bounds, if they vary, update non const loops (We
    // don't care about dim-0 since it doesn't affect the shape of the array)
    for (i = 1; i < WN_num_dim(wn); i++) {
        Update_Non_Const_Loops(WN_array_dim(wn,i), stack);
    }

    // Do some error checking for split commons
    // for a reference to a[i], check that the upper bound 
    // of i is smaller than the bounds of a.
    // Just catches the easy cases because at this late date, I'd rather
    // avoid false negatives than false positives
#ifdef LNO
    if (_num_vec == 1)
    {
        if (WN_operator(base) == OPR_LDA)
        {
#ifdef _NEW_SYMTAB
            if (ST_base_idx(WN_st(base)) != ST_st_idx(WN_st(base)) &&
#else
            if ((ST_sclass(WN_st(base)) == SCLASS_BASED) &&
#endif
                (ST_sclass(ST_base(WN_st(base))) == SCLASS_COMMON))
            {
                WN *array_dim = WN_array_dim(wn,0);
                if (WN_operator(array_dim) == OPR_INTCONST)
                {
                    INT64 dim = WN_const_val(array_dim);
                    if (!Too_Messy && !_dim[0].Too_Messy &&
                        !_dim[0].Contains_Lin_Symb() &&
                        !_dim[0].Contains_Non_Lin_Symb()) {
                    INT is_ai= TRUE;
                    IPL_ACCESS_VECTOR *av = &_dim[0];
                    for (INT i=0; i<av->Nest_Depth()-1; i++) {
                    if (av->Loop_Coeff(i)) is_ai = FALSE;
                }
                if (av->Loop_Coeff(av->Nest_Depth()-1) != 1) is_ai = FALSE;
                // check that the statement isn't conditional
                WN *parent = LWN_Get_Parent(wn);
                while (WN_opcode(parent) != OPC_DO_LOOP && is_ai) {
                    if (OPCODE_is_scf(WN_opcode(parent))) is_ai = FALSE;
                    parent = LWN_Get_Parent(parent);
                }
                if (is_ai) {
                    DO_LOOP_INFO *dli = Get_Do_Loop_Info(stack->Top_nth(0));
                    IPL_ACCESS_ARRAY *uba = dli->UB;
                    if (!uba->Too_Messy) {
                        if (uba->Num_Vec() == 1) {
                            IPL_ACCESS_VECTOR *ub = uba->Dim(0);
                            INT const_bound = TRUE;
                            if (!ub->Too_Messy && !ub->Contains_Lin_Symb() &&
                                    !ub->Contains_Non_Lin_Symb()) {
                                for (INT i=0; i<ub->Nest_Depth()-1; i++) {
                                    if (ub->Loop_Coeff(i)) const_bound = FALSE;
                                }
                                if (ub->Loop_Coeff(ub->Nest_Depth()-1) != 1) {
                                    const_bound = FALSE;
                                }
                                if (const_bound) {
                                    if (ub->Const_Offset > dim) {
                                        ErrMsgSrcpos(EC_LNO_Generic,Find_Line(wn),
                                                "Out of bounds array reference, results unpredictable."
                                                );
                                    }
                                }
                            }
                        }
                    }
                }
                }
                    }
                    }
        }
    }
#endif
}

#ifdef LNO


/*****************************************************************************
 *
 * Delinearize this access array, by delinearizing each dimension.
 *
 ****************************************************************************/

void IPL_ACCESS_ARRAY::Delinearize(DOLOOP_STACK *stack, WN *wn)
{
    if (Too_Messy) return;

    // Find a dimension that contains non-linear terms.
    INT i = 0;
    while (i < _num_vec
            && (Dim(i)->Too_Messy || !Dim(i)->Contains_Non_Lin_Symb())) i++;
    if (i == _num_vec) return;

    if (LNO_Debug_Delinearization) {
        fprintf(TFile, "Trying to delinearize\n");
        fprintf(TFile, "Before delinearization the access array was"); 
        Print(TFile);
        fprintf(TFile, "\n");
    }

    if (Delinearize(stack, i, wn)) { 
        // Call recursively as Dims might have completely changed.
        Delinearize(stack, wn);
        if (LNO_Debug_Delinearization) {
            fprintf(TFile, "succeeded\n");
            fprintf(TFile, "After delinearization the access array is"); 
            Print(TFile);
            fprintf(TFile, "\n");
        }
    } else if (LNO_Debug_Delinearization) {
        fprintf(TFile,"failed\n");
    }
}


/*****************************************************************************
 *
 * Try to delinearize the "dim"th dimension of this access, which must contain
 * some non-linear term.
 *
 * Return TRUE if successful.
 *
 ****************************************************************************/

INT IPL_ACCESS_ARRAY::Delinearize(DOLOOP_STACK *stack, INT dim, WN *wn)
{
    IPL_ACCESS_VECTOR *av = Dim(dim);
    IPL_SUMPROD_LIST *nl_symb = av->Non_Lin_Symb;

    Is_True(nl_symb != NULL && !nl_symb->Is_Empty(),
            ("IPL_ACCESS_ARRAY::Delinearize called on linear vector"));

    /* Look for a non-loop variable symbol that appears in every non-linear
     * term, by iterating over every symbol in the first non-linear term.
     */
    IPL_SUMPROD_CONST_ITER tmp_iter(nl_symb);
    IPL_SYMBOL_LIST *first = tmp_iter.First()->Prod_List;
    IPL_SYMBOL_CONST_ITER iter(first);
    for (const IPL_SYMBOL_NODE *node = iter.First(); !iter.Is_Empty();
            node = iter.Next())
    {
        if (node->Is_Loop_Var) continue;

        const IPL_SYMBOL *delin_symbol = &node->Symbol;

        /* Search for this symbol on all the other product terms. */
        BOOL this_one_good = TRUE;
        IPL_SUMPROD_CONST_ITER iter2(nl_symb);
        const IPL_SUMPROD_NODE *node2 = iter2.First();
        for (node2 = iter2.Next(); !iter2.Is_Empty() && this_one_good;
                node2 = iter2.Next()) {
            this_one_good = node2->Prod_List->Contains(delin_symbol);
        }
        if (this_one_good && av->Can_Delinearize(wn, delin_symbol)) {
            return Delinearize(stack, dim, delin_symbol);
        }
    }

    return FALSE;
}


/*****************************************************************************
 *
 * Given that "delin_symbol" is a non-loop variable present in every nonlinear
 * term, can we delinearize this vector? 
 *
 * "wn" is the array expression.
 *
 ****************************************************************************/

BOOL IPL_ACCESS_VECTOR::Can_Delinearize(WN *wn, const IPL_SYMBOL *delin_symbol)
{
    /* Create a new access vector that contains everything not multiplied by
     * "delin_symbol". This is essentially the remainder of dividing the
     * expression by "delin_symbol".
     */
    MEM_POOL_Push(&LNO_local_pool);

    IPL_ACCESS_VECTOR *tmp = CXX_NEW(IPL_ACCESS_VECTOR(Nest_Depth(), &LNO_local_pool),
            &LNO_local_pool);
    tmp->Too_Messy = FALSE;
    tmp->Const_Offset = Const_Offset;
    for (INT i = 0; i < Nest_Depth(); i++) {
        tmp->Set_Loop_Coeff(i, Loop_Coeff(i));
    }

    // Add linear terms that do not have "delin_symbol".
    tmp->Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST, &LNO_local_pool);
    IPL_INTSYMB_ITER lin_iter(Lin_Symb);
    for (IPL_INTSYMB_NODE *lin_node = lin_iter.First(); !lin_iter.Is_Empty();
            lin_node = lin_iter.Next())
    {
        if (!(lin_node->Symbol == *delin_symbol)) {
            tmp->Lin_Symb->Append(CXX_NEW(
                        IPL_INTSYMB_NODE(lin_node->Symbol,lin_node->Coeff),
                        &LNO_local_pool));
        }
    }

    /* In order to delinearize, we must prove that 0 <= tmp < delin_symbol. */

    // Is it possible that tmp < 0 (tmp + 1 <= 0)?
    tmp->Const_Offset = -tmp->Const_Offset-1; 
    if (Is_Consistent_Condition(tmp, wn)) {
        MEM_POOL_Pop(&LNO_local_pool);
        return FALSE;
    }
    // Is it possible that tmp >= delin_symbol (delin_symbol - tmp <= 0)
    // (where tmp is the original tmp)
    tmp->Const_Offset++;  // tmp <= 0
    tmp->Negate_Me();     // -tmp <= 0
    IPL_INTSYMB_NODE *delin_node = 
        CXX_NEW(IPL_INTSYMB_NODE(*delin_symbol,1),&LNO_local_pool);
    tmp->Lin_Symb->Prepend(delin_node);
    if (Is_Consistent_Condition(tmp, wn)) {
        MEM_POOL_Pop(&LNO_local_pool);
        return FALSE;
    }

    MEM_POOL_Pop(&LNO_local_pool);

    return TRUE;
}

#endif 


/*****************************************************************************
 *
 * Use the non-linear terms to update Non_Const_Loops i.e. given a[n*i], use i
 * to update Non_Const_Loops, which stores all loops with non-const coeff. We
 * assume that the effects of 'n' have already been taken care of.
 *
 ****************************************************************************/

void IPL_ACCESS_VECTOR::Update_Non_Const_Loops_Nonlinear(DOLOOP_STACK *stack)
{
    if (!Non_Lin_Symb) return;

    IPL_SUMPROD_CONST_ITER sp_iter(Non_Lin_Symb);
    for (const IPL_SUMPROD_NODE *sp_node = sp_iter.First();
            !sp_iter.Is_Empty(); sp_node = sp_iter.Next())
    {
        IPL_SYMBOL_LIST *sl = sp_node->Prod_List;
        IPL_SYMBOL_CONST_ITER iter(sl);
        for (const IPL_SYMBOL_NODE *node = iter.First(); !iter.Is_Empty();
                node=iter.Next())
        {
            if (!node->Is_Loop_Var) continue;

            IPL_SYMBOL symbol = node->Symbol;
            INT i = 0;
            while (! (IPL_SYMBOL(WN_index(stack->Bottom_nth(i))) == symbol)) i++;
            _non_const_loops = MAX(_non_const_loops, i+1);
        }
    }
}

#ifdef LNO

/*****************************************************************************
 *
 * Delinearize the "dim"th dimension w.r.t. symbol "delin_symbol". The result
 * is that this dimension is split into two consecutive dimensions: the first
 * is "dim / delin_symbol"; the second is "dim mod delin_symbol".
 *
 * Return FALSE if something went wrong (overflow).
 *
 ****************************************************************************/

INT IPL_ACCESS_ARRAY::Delinearize(DOLOOP_STACK *stack, INT dim,
        const IPL_SYMBOL *delin_symbol)
{
    IPL_ACCESS_VECTOR *av = Dim(dim);

    /* Create a new array of IPL_ACCESS_VECTORS, unfortunately, because it's an
     * array, we have to copy all the other dimensions.
     */
    IPL_ACCESS_VECTOR *new_dim =
        CXX_NEW_ARRAY(IPL_ACCESS_VECTOR, Num_Vec() + 1, _mem_pool);
    INT i = 0;
    for ( ; i < dim; i++) new_dim[i].Init(Dim(i), _mem_pool);
    for (i = dim+1; i < Num_Vec(); i++) {
        new_dim[i+1].Init(Dim(i), _mem_pool);
    }

    /* Set new_dim[dim] to the part of av that's multiplied by the symbol and
     * new_dim[dim+1] to the part of av that's not.
     */
    new_dim[dim].Init(av->Nest_Depth(), _mem_pool);
    new_dim[dim].Too_Messy = FALSE;
    new_dim[dim].Delinearized_Symbol =
        CXX_NEW(IPL_SYMBOL(delin_symbol), _mem_pool);

    // Search through the linear part of av, this affects the constant offset.
    IPL_INTSYMB_ITER lin_iter(av->Lin_Symb);
    IPL_INTSYMB_NODE *lin_node;
    for (lin_node = lin_iter.First(); !lin_iter.Is_Empty();
            lin_node = lin_iter.Next())
    {
        if (lin_node->Symbol == *delin_symbol) {
            INT64 coeff = new_dim[dim].Const_Offset + lin_node->Coeff;
            if ((coeff >= (INT32_MAX-1)) || (coeff<=(INT32_MIN+1))) {
                return FALSE;
            }
            new_dim[dim].Const_Offset = coeff;
        }
    }

    // Search through the non-linear part.
    IPL_SUMPROD_CONST_ITER nonlin_iter(av->Non_Lin_Symb);
    for (const IPL_SUMPROD_NODE *nonlin_node= nonlin_iter.First(); 
            !nonlin_iter.Is_Empty(); nonlin_node=nonlin_iter.Next())
    {
        IPL_SYMBOL_LIST *prod_list = nonlin_node->Prod_List;

        // check how many variables are on the prod list
        // if it's one or two, this is switching into a linear term
        INT length = prod_list->Len();
        if (length == 1)
        {
            INT64 coeff = new_dim[dim].Const_Offset + nonlin_node->Coeff;
            if ((coeff >= (INT32_MAX-1)) || (coeff<=(INT32_MIN+1))) {
                return FALSE;
            }
            new_dim[dim].Const_Offset = coeff;
        }
        else if (length==2)
        {
            // find the other one
            IPL_SYMBOL_CONST_ITER iter(prod_list);
            const IPL_SYMBOL_NODE *node = iter.First();
            if (node->Symbol == *delin_symbol) node = iter.Next();
            new_dim[dim].Add_Symbol(nonlin_node->Coeff,node->Symbol,stack,NULL);
            if (new_dim[dim].Too_Messy) return FALSE;
        }
        else
        {
            // it's still nonlinear
            IPL_SYMBOL_LIST *new_prod_list = CXX_NEW(IPL_SYMBOL_LIST,_mem_pool);
            IPL_SUMPROD_NODE *new_node = 
                CXX_NEW(IPL_SUMPROD_NODE(new_prod_list,nonlin_node->Coeff),_mem_pool);
            IPL_SYMBOL_CONST_ITER iter(nonlin_node->Prod_List);
            BOOL seen_delin_symbol = FALSE;
            for (const IPL_SYMBOL_NODE *node= iter.First();
                    !iter.Is_Empty(); node=iter.Next()) {
                if (seen_delin_symbol || !(node->Symbol == *delin_symbol)) { 
                    IPL_SYMBOL_NODE *new_symb_node = CXX_NEW(IPL_SYMBOL_NODE(node->Symbol,
                                node->Is_Loop_Var),_mem_pool);
                    new_prod_list->Append(new_symb_node);
                } else {
                    seen_delin_symbol = TRUE;
                }
            }
            if (!new_dim[dim].Non_Lin_Symb) {
                new_dim[dim].Non_Lin_Symb = CXX_NEW(IPL_SUMPROD_LIST,_mem_pool);
            }
            new_dim[dim].Non_Lin_Symb->Append(new_node);
        }
    }

    // new_dim[dim+1] is the same as the original tmp above
    new_dim[dim+1].Init(av->Nest_Depth(),_mem_pool);
    new_dim[dim+1].Too_Messy = FALSE;
    new_dim[dim+1].Const_Offset = av->Const_Offset;
    for (i=0; i<av->Nest_Depth(); i++) {
        new_dim[dim+1].Set_Loop_Coeff(i,av->Loop_Coeff(i));
    }
    new_dim[dim+1].Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST,_mem_pool);
    lin_iter.Init(av->Lin_Symb);
    for (lin_node=lin_iter.First(); !lin_iter.Is_Empty();
            lin_node = lin_iter.Next()) {
        if (!(lin_node->Symbol == *delin_symbol)) {
            new_dim[dim+1].Lin_Symb->Append(
                    CXX_NEW(IPL_INTSYMB_NODE(lin_node->Symbol,lin_node->Coeff),
                        _mem_pool));
        }
    }

    if (av->Non_Const_Loops()) {
        if (new_dim[dim].Contains_Non_Lin_Symb() ||
                new_dim[dim].Contains_Lin_Symb()) {
            new_dim[dim].Set_Non_Const_Loops(av->Non_Const_Loops());
        }
        if (new_dim[dim+1].Contains_Non_Lin_Symb() ||
                new_dim[dim+1].Contains_Lin_Symb()) {
            new_dim[dim+1].Set_Non_Const_Loops(av->Non_Const_Loops());
        }
    }

    _dim = new_dim;
    _num_vec++;

    return TRUE;
}
#endif

// Given an expression for the lb of a DO loop, set the access array
void IPL_ACCESS_ARRAY::Set_LB(WN *wn, DOLOOP_STACK *stack, INT64 step)
{
  Too_Messy = FALSE;
  if (step > 0) {
    // only top level maxs
    if ((Num_Vec() == 1) || (WN_operator(wn) == OPR_MAX)) {
      Set_LB_r(wn,stack,0,step);
    // there are maxs one level down
    } else if (WN_operator(wn) == OPR_SUB) {
      Set_LB_r(WN_kid0(wn),stack,0,step);
      for (INT i=0; i<Num_Vec(); i++) {
        _dim[i].Add(WN_kid1(wn),stack,-1);
      }
    } else if (WN_operator(wn) == OPR_ADD) {
      if (WN_operator(WN_kid0(wn)) == OPR_MAX) {
        Set_LB_r(WN_kid0(wn),stack,0,step);
        for (INT i=0; i<Num_Vec(); i++) {
          _dim[i].Add(WN_kid1(wn),stack,1);
        }
      } else {
        Set_LB_r(WN_kid1(wn),stack,0,step);
        for (INT i=0; i<Num_Vec(); i++) {
          _dim[i].Add(WN_kid0(wn),stack,1);
        }
      }
    }
  } else {
    // only top level mins
    if ((Num_Vec() == 1) || (WN_operator(wn) == OPR_MIN)) {
      Set_LB_r(wn,stack,0,step);
    // there are mins one level down
    } else if (WN_operator(wn) == OPR_SUB) {
      Set_LB_r(WN_kid0(wn),stack,0,step);
      for (INT i=0; i<Num_Vec(); i++) {
        _dim[i].Add(WN_kid1(wn),stack,1);
      }
    } else if (WN_operator(wn) == OPR_ADD) {
      if (WN_operator(WN_kid0(wn)) == OPR_MIN) {
        Set_LB_r(WN_kid0(wn),stack,0,step);
        for (INT i=0; i<Num_Vec(); i++) {
          _dim[i].Add(WN_kid1(wn),stack,-1);
        }
      } else {
        Set_LB_r(WN_kid1(wn),stack,0,step);
        for (INT i=0; i<Num_Vec(); i++) {
          _dim[i].Add(WN_kid0(wn),stack,-1);
        }
      }
    }
  }
}
// The recursive version of above, we're currently working on dim i 
// Return the next dimension to work on
INT IPL_ACCESS_ARRAY::Set_LB_r(WN *wn, DOLOOP_STACK *stack, INT i, INT64 step)
{
  if ((step > 0) && WN_operator(wn) == OPR_MAX) {
    INT res = Set_LB_r(WN_kid(wn,0),stack,i,step);
    res = Set_LB_r(WN_kid(wn,1),stack,res,step);
    return(res);
  } else if ((step < 0) && WN_operator(wn) == OPR_MIN) {
    INT res = Set_LB_r(WN_kid(wn,0),stack,i,step);
    res = Set_LB_r(WN_kid(wn,1),stack,res,step);
    return(res);
  } else if ((step > 0) && WN_operator(wn) ==
	     OPR_INTRINSIC_OP) {
    INT32 intr = WN_intrinsic(wn);
    if ((step > 0) &&
	((intr == INTRN_I4DIVFLOOR) || (intr == INTRN_I8DIVFLOOR) ||
	 (intr == INTRN_U4DIVFLOOR) || (intr == INTRN_U8DIVFLOOR))) {
      WN *const_kid = WN_kid0 (WN_kid1 (wn));
      if ((WN_operator (const_kid) == OPR_INTCONST) &&
	  (WN_const_val (const_kid) > 0) &&
	  (WN_const_val (const_kid) < INT32_MAX)) {
	FmtAssert (OPR_PARM == 
		   WN_operator (WN_kid0 (wn)),
		   ("Child of an intrn not a parm!"));
	_dim[i].Set(WN_kid0 (WN_kid0 (wn)), stack, 1, 
		    1 - WN_const_val (const_kid));
	_dim[i].Const_Offset = - _dim[i].Const_Offset;
	INT depth = _dim[i].Nest_Depth();
	_dim[i].Set_Loop_Coeff (depth-1, -WN_const_val (const_kid));
	_dim[i].Too_Messy = FALSE;
      } else 
	_dim[i].Too_Messy = TRUE;
      return (i+1);
    } else if ((step > 0) &&
	       ((intr == INTRN_I4DIVCEIL)||(intr == INTRN_I8DIVCEIL)||
		(intr == INTRN_U4DIVCEIL)||(intr == INTRN_U8DIVCEIL))) {
      WN *const_kid = WN_kid0 (WN_kid1 (wn));
      if ((WN_operator (const_kid) == OPR_INTCONST) &&
	  (WN_const_val (const_kid) > 0) &&
	  (WN_const_val (const_kid) < INT32_MAX)) {
	FmtAssert (OPR_PARM == 
		   WN_operator (WN_kid0 (wn)),
		   ("Child of an intrn not a parm!"));
	_dim[i].Set(WN_kid0 (WN_kid0 (wn)), stack, 1, 0);
	_dim[i].Const_Offset = - _dim[i].Const_Offset;
	INT depth = _dim[i].Nest_Depth();
	_dim[i].Set_Loop_Coeff (depth-1, -WN_const_val (const_kid));
	_dim[i].Too_Messy = FALSE;
      } else
	_dim[i].Too_Messy = TRUE;
      return (i+1);
    } else {
      _dim[i+1].Too_Messy = TRUE;
      return (i+1);
    }
  } else {
    INT depth = _dim[i].Nest_Depth();
    if (step > 0) {
      _dim[i].Set(wn,stack,1,0);
      if (_dim[i].Loop_Coeff(depth-1)) { // i = i + .. (i is on the rhs)
	_dim[i].Too_Messy = TRUE;
      } else {
	_dim[i].Set_Loop_Coeff (depth-1, -1);
	_dim[i].Const_Offset = - _dim[i].Const_Offset;
      }
    } else {
      _dim[i].Set(wn,stack,-1,0);
      if (_dim[i].Loop_Coeff(depth-1)) { // i = i + .. (i is on the rhs)
	_dim[i].Too_Messy = TRUE;
      } else {
	_dim[i].Set_Loop_Coeff (depth-1, 1);
	_dim[i].Const_Offset = - _dim[i].Const_Offset;
      }
    }
    return(i+1);
  }
}

// Given the comparison for the ub of a DO loop, set the access array
void IPL_ACCESS_ARRAY::Set_UB(WN *compare, DOLOOP_STACK *stack)
{
  Too_Messy = FALSE;
  INT sign,offset;

  if (WN_operator(compare) == OPR_LE) {
    sign = 1;
    offset = 0;
  } else if (WN_operator(compare) == OPR_GE) {
    sign = -1;
    offset = 0;
  } else if (WN_operator(compare) == OPR_LT) {
    sign = 1;
    offset = 1;
  } else if (WN_operator(compare) == OPR_GT) {
    sign = -1;
    offset = 1;
  } else {
    Is_True(0, ("IPL_ACCESS_ARRAY::Set_UB: Unknown comparison "));
  }

  if ((WN_operator(WN_kid0(compare)) == OPR_MIN) ||
      (WN_operator(WN_kid0(compare)) == OPR_MAX) ||
      (WN_operator(WN_kid0(compare))  == OPR_INTRINSIC_OP)) {
    for (INT i=0; i<Num_Vec(); i++) {
      _dim[i].Set(WN_kid1(compare),stack,-sign,offset);
    }
    if (!_dim[0].Too_Messy) {
      Set_UB_r(WN_kid0(compare),stack,0,-sign);
    }
  } else {
    for (INT i=0; i<Num_Vec(); i++) {
      _dim[i].Set(WN_kid0(compare),stack,sign,offset);
    }
    if (!_dim[0].Too_Messy) {
      Set_UB_r(WN_kid1(compare),stack,0,sign);
    }
  }
}

// The recursive version of above, we're currently working on dim i 
// Return the next dimension to work on
INT IPL_ACCESS_ARRAY::Set_UB_r(WN *wn, DOLOOP_STACK *stack, INT i, INT sign)
{
  OPERATOR oper = WN_operator(wn);
  if ((sign > 0) && oper == OPR_MIN) {
    INT res = Set_UB_r(WN_kid(wn,0),stack,i,sign);
    res = Set_UB_r(WN_kid(wn,1),stack,res,sign);
    return(res);
  } else if ((sign < 0) && oper == OPR_MAX) {
    INT res = Set_UB_r(WN_kid(wn,0),stack,i,sign);
    res = Set_UB_r(WN_kid(wn,1),stack,res,sign);
    return(res);
  } else if (oper == OPR_INTRINSIC_OP) {
    INT32 intr = WN_intrinsic(wn);
    if ((sign > 0) && 
	((intr == INTRN_I4DIVFLOOR) || (intr == INTRN_I8DIVFLOOR) ||
         (intr == INTRN_U4DIVFLOOR) || (intr == INTRN_U8DIVFLOOR))) {
      WN *const_kid = WN_kid0(WN_kid1(wn));
      if ((WN_operator(const_kid) == OPR_INTCONST) &&
	  (WN_const_val(const_kid) > 0)&&(WN_const_val(const_kid)<INT32_MAX)){
        _dim[i].Mul(WN_const_val(const_kid));
        _dim[i].Add(WN_kid0(WN_kid0(wn)),stack,-sign);
        _dim[i].Const_Offset = -_dim[i].Const_Offset;
        return(i+1);
      } else {
        _dim[i].Too_Messy = TRUE;
        return(i+1);
      }
    } else if ((sign < 0) && 
	((intr == INTRN_I4DIVCEIL) || (intr == INTRN_I8DIVCEIL) ||
         (intr == INTRN_U4DIVCEIL) || (intr == INTRN_U8DIVCEIL))) {
      WN *const_kid = WN_kid0(WN_kid1(wn));
      if ((WN_operator(const_kid) == OPR_INTCONST) &&
	  (WN_const_val(const_kid) > 0)&&(WN_const_val(const_kid)<INT32_MAX)) {
        _dim[i].Mul(WN_const_val(const_kid));
        _dim[i].Add(WN_kid0(WN_kid0(wn)),stack,-sign);
        _dim[i].Const_Offset = -_dim[i].Const_Offset;
        return(i+1);
      } else {
        _dim[i].Too_Messy = TRUE;
        return(i+1);
      }
    } else { 
      _dim[i].Too_Messy = TRUE;
      return(i+1);
    }
  } else {
    _dim[i].Add(wn,stack,-sign);
    _dim[i].Const_Offset = -_dim[i].Const_Offset;
    return(i+1);
  }
}

// Set the array for a condition statement
// we're currently working on dim i, return the next dimension to work on
INT IPL_ACCESS_ARRAY::Set_IF(WN *wn, DOLOOP_STACK *stack, BOOL negate, 
			  BOOL is_and, INT i)
{
  Too_Messy = FALSE;
  if (is_and && (WN_operator(wn) == OPR_LAND
      || WN_operator(wn) == OPR_CAND)) {
    INT res = Set_IF(WN_kid0(wn),stack,negate,is_and,i);
    return Set_IF(WN_kid1(wn),stack,negate,is_and,res);
  } 
  if (!is_and && (WN_operator(wn) == OPR_LIOR
      || WN_operator(wn) == OPR_CIOR)) {
    INT res = Set_IF(WN_kid0(wn),stack,negate,is_and,i);
    return Set_IF(WN_kid1(wn),stack,negate,is_and,res);
  } 
  _dim[i].Set_Condition(wn,stack,negate);
  return(i+1);
}

// set one component of the condition to the condition rooted at wn
// if negate is true, negate the condition
void IPL_ACCESS_VECTOR::Set_Condition(WN *wn, DOLOOP_STACK *stack, BOOL negate)
{
  Too_Messy = FALSE;

  if (WN_operator(wn) == OPR_LNOT) {
    wn = WN_kid0(wn);
    negate = !negate;
  }

  // 785479: 
  // i + (-2) <= 1 is NOT equivalent to i <= 3 
  // when the comparison is unsigned because of the overflow wraparound; 
  // this sequence came out of gccfe from the test (i == 2 || i == 3)
  if (OPERATOR_is_compare(WN_operator(wn)) && MTYPE_is_unsigned(WN_desc(wn))) {
    Too_Messy = TRUE;
    return;
  }
  
  INT sign,offset;
  sign = negate ? -1 : 1;
  if (WN_operator(wn) == OPR_LE) {
    offset = sign > 0 ? 0 : 1;
  } else if (WN_operator(wn) == OPR_GE) {
    offset = sign > 0 ? 0 : 1;
    sign = -sign;
  } else if (WN_operator(wn) == OPR_LT) {
    offset = sign > 0 ? 1 : 0;
  } else if (WN_operator(wn) == OPR_GT) {
    offset = sign > 0 ? 1 : 0;
    sign = -sign;
  } else if (WN_operator(wn) == OPR_INTCONST) {
    INT is_true = !(WN_const_val(wn) == 0);
    if (negate) is_true = !is_true;
    Set(wn,stack,0,0);
    if (is_true) {
      Const_Offset = 0;
    } else {
      Const_Offset = -1;
    }
    return;
  } else {
    Too_Messy = TRUE;
    return;
  }

  // initialize to the left hand side of the compare
  Set(WN_kid0(wn),stack,sign,offset);
  
  // add in the right hand side 
  if (!Too_Messy) {
    Add(WN_kid1(wn),stack,-sign);
    Const_Offset = -Const_Offset;
  }
}


/*****************************************************************************
 *
 * Given an expression and a stack of all the enclosing do loops, build the
 * access vector. We only build non-linear terms if allow_nonlin, otherwise,
 * if there are any, this vector is set to Too_Messy. The expression is first
 * multiplied by "sign", and then added by "offset".
 *
 ****************************************************************************/

void IPL_ACCESS_VECTOR::Set(WN *wn, DOLOOP_STACK *stack,
        INT8 sign, INT offset, BOOL allow_nonlin)
{
    Too_Messy = FALSE;
    Const_Offset = (INT64) offset;
    Non_Lin_Symb = NULL;
    Delinearized_Symbol = NULL;

    Add_Sum(wn, (INT64)sign, stack, allow_nonlin);
}

// Add coeff*(the term rooted at wn) to this
void IPL_ACCESS_VECTOR::Add(WN *wn, DOLOOP_STACK *stack, INT8 sign)
{
#ifdef LNO
    Add_Sum(wn, (INT64)sign, stack);
#else
    Add_Sum(wn, (INT64)sign, stack, LNO_Allow_Nonlinear);
#endif
}

// Add coeff*(expression represented by wn) to this vector
void IPL_ACCESS_VECTOR::Add_Sum(WN *wn, INT64 coeff, DOLOOP_STACK *stack,
        BOOL allow_nonlin)
{
    if (Too_Messy) return;

    switch (WN_operator(wn))
    {
        case OPR_ADD:
            Add_Sum(WN_kid(wn,0), coeff, stack, allow_nonlin);
            Add_Sum(WN_kid(wn,1), coeff, stack, allow_nonlin);
            break;

        case OPR_SUB:
            Add_Sum(WN_kid(wn,0), coeff, stack, allow_nonlin);
            Add_Sum(WN_kid(wn,1), -coeff, stack, allow_nonlin);
            break;

        case OPR_NEG:
            Add_Sum(WN_kid(wn,0), -coeff, stack, allow_nonlin);
            break;

        case OPR_MPY:
            if (WN_operator(WN_kid(wn,0)) == OPR_INTCONST)
            {
                Add_Sum(WN_kid(wn,1), coeff*WN_const_val(WN_kid(wn,0)),
                        stack, allow_nonlin);
            }
            else if (WN_operator(WN_kid(wn,1)) == OPR_INTCONST)
            {
                Add_Sum(WN_kid(wn,0), coeff*WN_const_val(WN_kid(wn,1)),
                        stack, allow_nonlin);
            }
            else if (allow_nonlin
                    && (coeff < INT32_MAX-1) && (coeff > INT32_MIN+1))
            {
                MEM_POOL_Push(&LNO_local_pool);

                /* Create a new non-linear term and merge it with the existing
                 * list if exist.
                 */
                IPL_SUMPROD_LIST *list = CXX_NEW(IPL_SUMPROD_LIST,
                        (Non_Lin_Symb != NULL) ? &LNO_local_pool : _mem_pool);
                IPL_SYMBOL_LIST *sl = CXX_NEW(IPL_SYMBOL_LIST, _mem_pool);
                list->Append(CXX_NEW(IPL_SUMPROD_NODE(sl, coeff), _mem_pool));

                list = Add_Nonlin(wn, list, stack);
                if (list != NULL) {
                    if (Non_Lin_Symb == NULL) {
                        Non_Lin_Symb = list;
                    } else {
                        Non_Lin_Symb->Merge(list);
                    }
                } else {
                    Too_Messy = TRUE;
                }

                /* Some of the non-linear symbols may actually be linear.
                 * Go through the list and move those to the linear list.
                 * e.g. given n*(i-1), both n*i and n*-1 will be on non-linear
                 * list, we want to move n*-1 to the linear list.
                 */
                IPL_SUMPROD_ITER sp_iter(Non_Lin_Symb);
                IPL_SUMPROD_NODE *sp_prev = NULL, *sp_next = NULL;
                for (IPL_SUMPROD_NODE *sp_node = sp_iter.First();
                        !sp_iter.Is_Empty(); sp_node = sp_next)
                {
                    sp_next = sp_iter.Next();

                    IPL_SYMBOL_LIST *sl = sp_node->Prod_List;
                    INT length = sl->Len();
                    IPL_SYMBOL_ITER iter(sl);
                    IPL_SYMBOL_NODE *node = iter.First(); 

                    BOOL remove_term = FALSE;

                    if (node == NULL)
                    {
                        // The non-linear term is a constant.
                        Const_Offset += sp_node->Coeff;
                        remove_term = TRUE;
                    }
                    else if (length == 1)
                    {
                        // The term has only one symbol, so really linear.
                        Add_Symbol(sp_node->Coeff, node->Symbol, stack, NULL);
                        remove_term = TRUE;
                    }

                    if (remove_term) {
                        // Remove the term.
                        sp_node = (sp_prev != NULL) ?
                                Non_Lin_Symb->Remove(sp_prev, sp_node) :
                                Non_Lin_Symb->Remove_Headnode();
                        CXX_DELETE(sp_node, _mem_pool);
                        // NOTE: no need to update sp_prev.
                    } else {
                        sp_prev = sp_node;
                    }
                }

                MEM_POOL_Pop(&LNO_local_pool);
            }
            else
            {
                Too_Messy = TRUE;
            }
            break;

        case OPR_LDID:
        {
            IPL_SYMBOL symb(wn);
            Add_Symbol((INT64)coeff, symb, stack, wn);
            break;
        }

        case OPR_INTCONST:
            // DAVID: what is the point?
            if (coeff == 1) {
                Const_Offset += WN_const_val(wn);
            } else if (coeff == -1) {
                Const_Offset -= WN_const_val(wn);
            } else {
                Const_Offset += coeff*WN_const_val(wn);
            }
            break;

        case OPR_PAREN:
            Add_Sum(WN_kid(wn,0), coeff, stack, allow_nonlin);
            break;

        default:
            if (WN_opcode(wn) == OPC_I8I4CVT
#ifdef PATHSCALE_MERGE
                || WN_opcode(wn) == OPC_U8I4CVT
#endif
               )
            {
                Add_Sum(WN_kid(wn,0), coeff, stack, allow_nonlin);
#ifdef KEY
            } else if (WN_opcode(wn) == OPC_I4U8CVT &&
                    WN_opcode(WN_kid0(wn)) == OPC_U8CVTL &&
                    WN_cvtl_bits(WN_kid0(wn)) == 32) {
                // Bug 4525 - tolerate CVTs in the access vector for -m64
                // compilation when the type of loop variable is I8 but the
                // rest of the ARRAY kids are of type U4/I4. The CVT and the
                // associated CVTL introduced by the front-end or inliner can
                // be ignored. The return type can be assumed to be of type
                // I4.
                Add_Sum(WN_kid0(WN_kid0(wn)), coeff, stack, allow_nonlin);    
#endif
            } else {
                Too_Messy = TRUE;
            }
            break;
    }
}


/*****************************************************************************
 *
 * Add to the vector the term coeff*symbol, if it's not null.
 * "wn" is the WHIRL node of the load and update non_const_loops for "wn".
 *
 ****************************************************************************/

void IPL_ACCESS_VECTOR::Add_Symbol(INT64 coeff, IPL_SYMBOL symbol, 
        DOLOOP_STACK *stack, WN *wn)
{
    if (wn != NULL && TY_is_volatile(WN_ty(wn))) {
        Too_Messy = TRUE;
        return;
    }

    if ((coeff >= (INT32_MAX-1)) || (coeff<=(INT32_MIN+1))) {
        Too_Messy = TRUE;  // Overflow
        return;
    }

    /* Is it a loop induction/index variable? */
    BOOL is_iv = FALSE;
    INT32 i;
    for (i = 0; i < stack->Elements() && !is_iv; i++) {
        IPL_SYMBOL doloop(WN_index(stack->Bottom_nth(i)));
        if (symbol == doloop) is_iv = TRUE;
    }

    if (is_iv)
    {
        if (_lcoeff == NULL) {
            _lcoeff = CXX_NEW_ARRAY(mINT32, _nest_depth, _mem_pool);
            for (INT j = 0; j < _nest_depth; j++) _lcoeff[j] = 0;
        }

        coeff += _lcoeff[i-1];

        if ((coeff >= (INT32_MAX-1)) || (coeff<=(INT32_MIN+1))) {
            Too_Messy = TRUE;  // Overflow
            return;
        }

        _lcoeff[i-1] = coeff;
    }
    else
    {
        // it's a symbolic
        if (Lin_Symb == NULL) {
            Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST,_mem_pool);
        } else {
            // check to see if it's already on the linear list
            IPL_INTSYMB_ITER iter(Lin_Symb);
            IPL_INTSYMB_NODE* prevnode = NULL; 
            for (IPL_INTSYMB_NODE *node = iter.First(); !iter.Is_Empty(); 
                    node=iter.Next()) {
                if (node->Symbol == symbol) {
                    coeff += node->Coeff;
                    if ((coeff >= (INT32_MAX-1)) || (coeff<=(INT32_MIN+1))) {
                        Too_Messy = TRUE;  // Overflow
                        return;
                    }
                    node->Coeff = coeff;
                    if (node->Coeff == 0) { // get rid of it
                        if (node == iter.First()) {
                            CXX_DELETE(Lin_Symb->Remove_Headnode(),_mem_pool);
                        } else {
                            CXX_DELETE(Lin_Symb->Remove(prevnode,node),_mem_pool);
                        }
                    }
                    if (wn) Update_Non_Const_Loops(wn,stack);
                    return;
                }
                prevnode = node; 
            }
        }

        // it's a new symbol, so add it
        Lin_Symb->Prepend(CXX_NEW(IPL_INTSYMB_NODE(symbol,coeff),_mem_pool));
        if (wn) Update_Non_Const_Loops(wn,stack);
    } 
}


/*****************************************************************************
 *
 * Given the input list, return the output list resulting from multiplying
 * "wn" by the input list.
 *
 * The parameters cannot be NULL.
 * If too messy, return NULL and Too_Messy is set.
 *
 ****************************************************************************/

IPL_SUMPROD_LIST *IPL_ACCESS_VECTOR::Add_Nonlin(WN *wn, IPL_SUMPROD_LIST *list,
        DOLOOP_STACK *stack)
{
    Is_True(list != NULL , ("Null input list in IPL_ACCESS_VECTOR::Add_Nonlin"));

    if (Too_Messy) return NULL;

    switch (WN_operator(wn))
    {
        case OPR_ADD:
        {
            IPL_SUMPROD_LIST *list2 = CXX_NEW(IPL_SUMPROD_LIST(list,_mem_pool),
                    &LNO_local_pool);
            list2 = Add_Nonlin(WN_kid1(wn), list2, stack);
            list = Add_Nonlin(WN_kid0(wn), list, stack);
            if (!Too_Messy) list->Merge(list2);
            return list;
        }

        case OPR_SUB:
        {
            IPL_SUMPROD_LIST *list2 = CXX_NEW(IPL_SUMPROD_LIST(list,_mem_pool), 
                    &LNO_local_pool);
            list2 = Add_Nonlin(WN_kid1(wn), list2, stack);
            if (!list2->Negate_Me()) {
                Too_Messy = TRUE;
                return NULL;
            }
            list = Add_Nonlin(WN_kid0(wn), list, stack);
            if (!Too_Messy) list->Merge(list2);
            return list;
        }

        case OPR_NEG:
            if (!list->Negate_Me()) {
                Too_Messy = TRUE;
                return NULL;
            }
            return Add_Nonlin(WN_kid0(wn), list, stack);

        case OPR_MPY:
            list = Add_Nonlin(WN_kid0(wn), list, stack);
            if (list != NULL) list = Add_Nonlin(WN_kid1(wn), list, stack);
            return list;

        case OPR_INTCONST:
        {
            INT64 offset = WN_const_val(wn);
            IPL_SUMPROD_ITER iter(list);
            for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty();
                    node = iter.Next()) {
                INT64 coeff = node->Coeff*offset;
                // Check for overflow.
                if ((coeff >= (INT32_MAX-1)) || (coeff<=(INT32_MIN+1))) {
                    Too_Messy = TRUE;
                    return NULL;
                }
                node->Coeff = coeff;
            }
            return list;
        }

        case OPR_LDID:
        {
            // Check if it is a loop index variable.
            BOOL is_iv = FALSE;
            IPL_SYMBOL symbol(wn);
            for (INT32 i = 0; i < stack->Elements() && !is_iv; i++) {
                IPL_SYMBOL doloop(WN_index(stack->Bottom_nth(i)));
                if (symbol == doloop) is_iv = TRUE;
            }

            IPL_SUMPROD_ITER iter(list);
            for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty();
                    node = iter.Next()) {
                node->Prod_List->Append(
                        CXX_NEW(IPL_SYMBOL_NODE(symbol, is_iv), _mem_pool));
            }

            // if it's not an iv, update Non_Const_Loops
            // we'll deal with iv's after we try to delinearize
            if (!is_iv) Update_Non_Const_Loops(wn, stack);

            return list;
        }

        default:
            Too_Messy = TRUE;
    }

    return NULL;
}

// for all the loads inside the expression node wn, 
// use the def-use chains to update Non_Const_Loops
void IPL_ACCESS_VECTOR::Update_Non_Const_Loops(WN *wn, DOLOOP_STACK *stack)
{
    OPCODE opc = WN_opcode(wn);
    if (OPCODE_is_load(opc)) {
        if (OPCODE_operator(opc) != OPR_LDID) {
            _non_const_loops = stack->Elements();
            return;
        }
    } else {
        for (INT kidno=0; kidno<WN_kid_count(wn); kidno++) {
            Update_Non_Const_Loops(WN_kid(wn,kidno),stack);
        }
    }

    // it's an ldid

    DEF_LIST *defs = Du_Mgr->Ud_Get_Def(wn);

    // nenad, 02/15/2000: 
    // We should also set _non_const_loops conservatively if
    // defs->Incomplete(), but that causes performance problems.
    if (!defs) {
        _non_const_loops = stack->Elements();
        return;
    }
    DEF_LIST_ITER iter(defs);

    for(DU_NODE *node=iter.First(); !iter.Is_Empty(); node=iter.Next()) {
        WN *def = node->Wn();

        // find the inner loop surrounding the def
        while (def && (WN_opcode(def) != OPC_DO_LOOP)) {
            def = LWN_Get_Parent(def);
        }
        if (def) {  // there is a do loop surrounding the def, find out which one
            def = LNO_Common_Loop(def,wn);
            if (def) {
                INT i=0;
                INT num_elements = stack->Elements();
                while ((i < num_elements) && (def != stack->Bottom_nth(i))) {
                    i++;
                }
                if (i < num_elements) {  // it varies in an ancestor loops
                    _non_const_loops = MAX(_non_const_loops,i+1);
                }
            }
        }
    }
}

// for all the loads inside the expression node wn, 
// use the def-use chains to update Non_Const_Loops for every access
// vector in this array (this is used when the base or the bounds vary)
void IPL_ACCESS_ARRAY::Update_Non_Const_Loops(WN *wn, DOLOOP_STACK *stack)
{
    OPCODE opc = WN_opcode(wn);
    if (OPCODE_is_load(opc)) {
        if (OPCODE_operator(opc) != OPR_LDID) {
            for (INT32 i=0; i<_num_vec; i++) {
                Dim(i)->Max_Non_Const_Loops(stack->Elements());
            }
            return;
        }
    } else {
        for (INT kidno=0; kidno<WN_kid_count(wn); kidno++) {
            Update_Non_Const_Loops(WN_kid(wn,kidno),stack);
        }
        return;
    }

    // it's an ldid
    DEF_LIST *defs = Du_Mgr->Ud_Get_Def(wn);

    // nenad, 02/15/2000: 
    // We should also set _non_const_loops conservatively if
    // defs->Incomplete(), but that causes performance problems.
    if (!defs) {
        for (INT32 i=0; i<_num_vec; i++) {
            Dim(i)->Max_Non_Const_Loops(stack->Elements());
        }
        return;
    }


    DEF_LIST_ITER iter(defs);

    INT max = 0;
    for(const DU_NODE *node=iter.First(); !iter.Is_Empty(); node=iter.Next()) {
        const WN *def = node->Wn();

        // find the inner loop surrounding the def
        while (def && (WN_opcode(def) != OPC_DO_LOOP)) {
            def = LWN_Get_Parent(def);
        }
        if (def) {  // there is a do loop surrounding the def, find out which one
            INT i=0;
            INT num_elements = stack->Elements();
            while ((i < num_elements) && (def != stack->Bottom_nth(i))) {
                i++;
            }
            if (i < num_elements) {  // it varies in an ancestor loops
                max = MAX(max,i+1);
            }
        }
    }
    if (max > 0) {
        for (INT32 i=0; i<_num_vec; i++) {
            Dim(i)->Max_Non_Const_Loops(max);
        }
    }
}



IPL_ACCESS_VECTOR *Subtract(IPL_ACCESS_VECTOR *v1, IPL_ACCESS_VECTOR *v2,
			MEM_POOL *mem_pool)
{
  Is_True(v1 && v2, ("Access vector subtraction requires non-nil operands"));

  if (v1->_nest_depth != v2->_nest_depth)
    return NULL;

  IPL_ACCESS_VECTOR *rv = CXX_NEW(IPL_ACCESS_VECTOR, mem_pool);

  rv->Too_Messy = (v1->Too_Messy || v2->Too_Messy);

  if (rv->Too_Messy)
    return rv;

  // TODO actually, we want to compute the rv and then call some function
  // to compute the actual depth.  That function has not been written yet,
  // though, so I'm doing something conservative.

  rv->_non_const_loops = MAX(v1->_non_const_loops, v2->_non_const_loops);
  rv->_nest_depth = v1->_nest_depth;
  rv->_mem_pool = mem_pool;
  rv->Const_Offset = v1->Const_Offset - v2->Const_Offset;

  rv->_lcoeff = CXX_NEW_ARRAY(mINT32, rv->_nest_depth, rv->_mem_pool);
  for (INT i=0; i < rv->_nest_depth; i++)
    rv->_lcoeff[i] = (v1->_lcoeff ? v1->_lcoeff[i] : 0) -
                     (v2->_lcoeff ? v2->_lcoeff[i] : 0);

  rv->Lin_Symb = Subtract(v1->Lin_Symb, v2->Lin_Symb, rv->_mem_pool);

  rv->Non_Lin_Symb = CXX_NEW(IPL_SUMPROD_LIST, rv->_mem_pool);
  if (v1->Non_Lin_Symb)
    rv->Non_Lin_Symb->Init(v1->Non_Lin_Symb, rv->_mem_pool);
  if (v2->Non_Lin_Symb) {
    IPL_SUMPROD_ITER iter(v2->Non_Lin_Symb);
    for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty(); 
	 node=iter.Next()) {
      // TODO again conservative, because in theory an entry on v1 and v2
      // could cancel each other.
      IPL_SUMPROD_NODE *n = CXX_NEW(IPL_SUMPROD_NODE(node,mem_pool),mem_pool);
      n->Coeff = - n->Coeff;
      rv->Non_Lin_Symb->Append(n);
    }
  }

  return rv;
}

IPL_ACCESS_VECTOR *Add(IPL_ACCESS_VECTOR *v1, IPL_ACCESS_VECTOR *v2,
			MEM_POOL *mem_pool)
{
  Is_True(v1 && v2, ("Access vector subtraction requires non-nil operands"));

  if (v1->_nest_depth != v2->_nest_depth)
    return NULL;

  IPL_ACCESS_VECTOR *rv = CXX_NEW(IPL_ACCESS_VECTOR, mem_pool);

  rv->Too_Messy = (v1->Too_Messy || v2->Too_Messy);

  if (rv->Too_Messy)
    return rv;

  // TODO actually, we want to compute the rv and then call some function
  // to compute the actual depth.  That function has not been written yet,
  // though, so I'm doing something conservative.

  rv->_non_const_loops = MAX(v1->_non_const_loops, v2->_non_const_loops);
  rv->_nest_depth = v1->_nest_depth;
  rv->_mem_pool = mem_pool;
  rv->Const_Offset = v1->Const_Offset + v2->Const_Offset;

  rv->_lcoeff = CXX_NEW_ARRAY(mINT32, rv->_nest_depth, rv->_mem_pool);
  for (INT i=0; i < rv->_nest_depth; i++)
    rv->_lcoeff[i] = (v1->_lcoeff ? v1->_lcoeff[i] : 0) +
                     (v2->_lcoeff ? v2->_lcoeff[i] : 0);

  rv->Lin_Symb = Add(v1->Lin_Symb, v2->Lin_Symb, rv->_mem_pool);

  rv->Non_Lin_Symb = CXX_NEW(IPL_SUMPROD_LIST, rv->_mem_pool);
  if (v1->Non_Lin_Symb)
    rv->Non_Lin_Symb->Init(v1->Non_Lin_Symb, rv->_mem_pool);
  if (v2->Non_Lin_Symb) {
    IPL_SUMPROD_ITER iter(v2->Non_Lin_Symb);
    for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty(); 
	 node=iter.Next()) {
      // TODO again conservative, because in theory an entry on v1 and v2
      // could cancel each other.
      IPL_SUMPROD_NODE *n = CXX_NEW(IPL_SUMPROD_NODE(node,mem_pool),mem_pool);
      rv->Non_Lin_Symb->Append(n);
    }
  }

  return rv;
}

IPL_ACCESS_VECTOR *Mul(INT c, IPL_ACCESS_VECTOR *v, MEM_POOL *mem_pool)
{
  Is_True(v, ("Access vector multiplication requires non-nil operand"));

  IPL_ACCESS_VECTOR *rv = CXX_NEW(IPL_ACCESS_VECTOR(v, mem_pool), mem_pool);

  if (rv->Too_Messy)
    return rv;

  for (INT i=0; i< rv->_nest_depth; i++)
    rv->_lcoeff[i] *= c;

  rv->Lin_Symb = Mul(c, v->Lin_Symb, rv->_mem_pool);

  if (v->Non_Lin_Symb) {
    IPL_SUMPROD_ITER iter(v->Non_Lin_Symb);
    for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty();
	 node=iter.Next()) {
      node->Coeff *= c;
    }
  }

  return rv;
}

IPL_ACCESS_VECTOR *Merge(IPL_ACCESS_VECTOR *v1, IPL_ACCESS_VECTOR *v2,
		     MEM_POOL *mem_pool)
{
  Is_True(v1 && v2, ("Access vector subtraction requires non-nil operands"));

//  if (v1->_nest_depth != v2->_nest_depth)
//   return NULL;

  IPL_ACCESS_VECTOR *rv = CXX_NEW(IPL_ACCESS_VECTOR, mem_pool);

  // Use the mininum nest_depth as the new depth
  rv->_nest_depth = MIN(v1->_nest_depth, v2->_nest_depth);
  rv->Too_Messy = (v1->Too_Messy || v2->Too_Messy);

  if (rv->Too_Messy)
    return rv;

  rv->_non_const_loops = MAX(v1->_non_const_loops, v2->_non_const_loops);
  rv->_mem_pool = mem_pool;
  rv->Const_Offset = v1->Const_Offset + v2->Const_Offset;

  rv->_lcoeff = CXX_NEW_ARRAY(mINT32, rv->_nest_depth, rv->_mem_pool);
  for (INT i=0; i < rv->_nest_depth; i++)
    rv->_lcoeff[i] = (v1->_lcoeff ? v1->_lcoeff[i] : 0) +
                     (v2->_lcoeff ? v2->_lcoeff[i] : 0);

  rv->Lin_Symb = Add(v1->Lin_Symb, v2->Lin_Symb, rv->_mem_pool);

  rv->Non_Lin_Symb = CXX_NEW(IPL_SUMPROD_LIST, rv->_mem_pool);
  if (v1->Non_Lin_Symb)
    rv->Non_Lin_Symb->Init(v1->Non_Lin_Symb, rv->_mem_pool);
  if (v2->Non_Lin_Symb) {
    IPL_SUMPROD_ITER iter(v2->Non_Lin_Symb);
    for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty(); 
	 node=iter.Next()) {
      // TODO again conservative, because in theory an entry on v1 and v2
      // could cancel each other.
      IPL_SUMPROD_NODE *n = CXX_NEW(IPL_SUMPROD_NODE(node,mem_pool),mem_pool);
      rv->Non_Lin_Symb->Append(n);
    }
  }

  return rv;
}

void IPL_ACCESS_VECTOR::Mul(INT c)
{
  if (Too_Messy) return;
  for (INT i=0; i< _nest_depth; i++) {
    if (_lcoeff[i]) {
      INT64 prod = _lcoeff[i] * c;
      if (prod < INT32_MAX) {
	_lcoeff[i] = prod;
      } else {
	Too_Messy = TRUE;
	return;
      }
    }
  }
  if (Lin_Symb) {
    IPL_INTSYMB_ITER ii(Lin_Symb);
    for (IPL_INTSYMB_NODE *in = ii.First(); !ii.Is_Empty(); in = ii.Next()) {
      if (in->Coeff == 1) {
	in->Coeff = c;
      } else {
        INT64 prod = in->Coeff * c;
        if (prod < INT32_MAX) {
	  in->Coeff = prod;
        } else {
	  Too_Messy = TRUE;
	  return;
        }
      }
    }
  }
  if (Non_Lin_Symb) {
    IPL_SUMPROD_ITER iter(Non_Lin_Symb);
    for (IPL_SUMPROD_NODE *node = iter.First(); !iter.Is_Empty();
	 node=iter.Next()) {
      if (node->Coeff == 1) {
	node->Coeff = c;
      } else {
        INT64 prod = node->Coeff * c;
        if (prod < INT32_MAX) {
	  node->Coeff = prod;
        } else {
	  Too_Messy = TRUE;
	  return;
        }
      }
    }
  }
}

void IPL_ACCESS_VECTOR::Negate_Me()
{
  if (Too_Messy)
    return;

  Const_Offset = -Const_Offset;

  if (_lcoeff) {
    for (INT i=0; i<_nest_depth; i++)
      _lcoeff[i] = -_lcoeff[i];
  }

  if (Contains_Lin_Symb()) {
    IPL_INTSYMB_ITER ii(Lin_Symb);
    for (IPL_INTSYMB_NODE *in = ii.First(); !ii.Is_Empty(); in = ii.Next())
      in->Coeff = -in->Coeff;
  }

  if (Contains_Non_Lin_Symb()) {
    IPL_SUMPROD_ITER si(Non_Lin_Symb);
    for (IPL_SUMPROD_NODE *sn = si.First(); !si.Is_Empty(); sn = si.Next())
      sn->Coeff = -sn->Coeff;
  }
}

IPL_ACCESS_VECTOR *IPL_ACCESS_VECTOR::Convert_Bound_To_Exp(MEM_POOL *pool)
{
  IPL_ACCESS_VECTOR *result = CXX_NEW(IPL_ACCESS_VECTOR(this,pool), pool);
  if (Too_Messy) return(result);

  if(_lcoeff[_nest_depth-1] > 0) { // an upper bound, negate all the 
				   // loop variables and symbols
    for (INT i=0; i<_nest_depth-1; i++) {
      result->_lcoeff[i] = -_lcoeff[i];
    }
    IPL_INTSYMB_ITER ii(result->Lin_Symb);
    for (IPL_INTSYMB_NODE *in = ii.First(); !ii.Is_Empty(); in = ii.Next())
      in->Coeff = -in->Coeff;
  
    IPL_SUMPROD_ITER si(result->Non_Lin_Symb);
    for (IPL_SUMPROD_NODE *sn = si.First(); !si.Is_Empty(); sn = si.Next())
      sn->Coeff = -sn->Coeff;
  } else { // a lower bound, just negate the constant
    result->Const_Offset = -result->Const_Offset;
  }
  result->_lcoeff[_nest_depth-1] = 0;

  return(result);
}


// How many maxs are there rooted in this tree
// Only count maxs which are either top level or children of maxs
extern INT Num_Maxs(WN *wn)
{
  if (WN_operator(wn) == OPR_MAX) {
    return(1+Num_Maxs(WN_kid(wn,0))+Num_Maxs(WN_kid(wn,1)));
  } else {
    return 0;
  }
}

// How many mins are there rooted in this tree
// Only count mins which are either top level or children of mins
extern INT Num_Mins(WN *wn)
{
  if (WN_operator(wn) == OPR_MIN) {
    return(1+Num_Mins(WN_kid(wn,0))+Num_Mins(WN_kid(wn,1)));
  } else {
    return 0;
  }
}

// How many logical ands are there rooted in this tree
// Only count LANDs which are either top level or children of LANDs
INT Num_Lands(WN *wn)
{
  if (WN_operator(wn) == OPR_LAND
      || WN_operator(wn) == OPR_CAND) {
    return(1+Num_Lands(WN_kid(wn,0))+Num_Lands(WN_kid(wn,1)));
  } else {
    return 0;
  }
}

// How many logical ors are there rooted in this tree
// Only count LIORs which are either top level or children of LIORs
INT Num_Liors(WN *wn)
{
  if (WN_operator(wn) == OPR_LIOR
      || WN_operator(wn) == OPR_CIOR) {
    return(1+Num_Liors(WN_kid(wn,0))+Num_Liors(WN_kid(wn,1)));
  } else {
    return 0;
  }
}


//=======================================================================
//
// Build an access vector according to line 'i' of 'soe',
// using the symbols in 'syms' for linear terms.  The dimension of the system
// is given in 'dim', the number of enclosing loops is 'depth'.
// Which array to use for coefficients is controlled by 'which_array':
//   which_array=0 (Work) which_array=1 (Eq) which_array=2 (Le).
// This is very ugly indeed.
//
//=======================================================================
IPL_ACCESS_VECTOR::IPL_ACCESS_VECTOR(const SYSTEM_OF_EQUATIONS *soe,
			     const INT i, const IPL_SYMBOL_LIST *syms,
			     const INT depth, const INT dim,
			     const INT non_const_loops,
			     const INT which_array,
			     BOOL is_lower_bound, MEM_POOL *pool)
{
  INT k;

  _mem_pool = pool;
  _nest_depth = depth;
  _non_const_loops = non_const_loops;

  // TODO: need to fix Delinearized_Symbol
  Too_Messy = FALSE;
  Non_Lin_Symb = NULL;
  Lin_Symb = NULL;
  Delinearized_Symbol = NULL;
  _lcoeff = CXX_NEW_ARRAY(mINT32,_nest_depth,_mem_pool);

  switch (which_array) {
  case 0:
    {
 
      if (is_lower_bound) {
	for (k = 0; k < _nest_depth; ++k) 
	  _lcoeff[k] = soe->Work(i,dim+k);
	Const_Offset = -soe->Work_Const(i);
      
	// Set the Lin_Symb list
	IPL_SYMBOL_CONST_ITER iter(syms);

	for (const IPL_SYMBOL_NODE *s = iter.First(); 
	     k+dim < soe->Num_Vars() && !iter.Is_Empty();
	     ++k, s = iter.Next()) {
	  if (soe->Work(i,k+dim)) {
	    if (Lin_Symb == NULL) Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST, _mem_pool);
	    Lin_Symb->Append(CXX_NEW(IPL_INTSYMB_NODE(s->Symbol,soe->Work(i,k+dim)),_mem_pool));
	  }
	} 
      } else {
	for (k = 0; k < _nest_depth; ++k) 
	  _lcoeff[k] = -soe->Work(i,dim+k);
	Const_Offset = soe->Work_Const(i);
    
	// Set the Lin_Symb list
	IPL_SYMBOL_CONST_ITER iter(syms);
	
	for (const IPL_SYMBOL_NODE *s = iter.First(); 
	     k+dim < soe->Num_Vars() && !iter.Is_Empty();
	     ++k, s = iter.Next()) {
	  if (soe->Work(i,k+dim)) {
	    if (Lin_Symb == NULL) Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST, _mem_pool);
	    Lin_Symb->Append(CXX_NEW(IPL_INTSYMB_NODE(s->Symbol,-soe->Work(i,k+dim)),_mem_pool));
	  }
	}
      }
    }
    break;

  case 1:
    {
      const IMAT &aeq = soe->Aeq();
      const INT64 *beq = soe->Beq();

      // Must be lower_bound
      for (k = 0; k < _nest_depth; ++k) 
	_lcoeff[k] = -aeq(i,dim+k);
      Const_Offset = beq[i];
    
      // Set the Lin_Symb list
      IPL_SYMBOL_CONST_ITER iter(syms);

      for (const IPL_SYMBOL_NODE *s = iter.First(); 
	   k+dim < soe->Num_Vars() && !iter.Is_Empty();
	   ++k, s = iter.Next()) {
	if (aeq(i,k+dim)) {
	  if (Lin_Symb == NULL) Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST, _mem_pool);
	  Lin_Symb->Append(CXX_NEW(IPL_INTSYMB_NODE(s->Symbol,aeq(i,k+dim)),_mem_pool));
	}
      } 
    }
    break;

  case 2:
    {
      const IMAT &ale = soe->Ale();
      const INT64 *ble = soe->Ble();

      if (is_lower_bound) {
	for (k = 0; k < _nest_depth; ++k) 
	  _lcoeff[k] = ale(i,dim+k);
	Const_Offset = -ble[i];
    
	// Set the Lin_Symb list
	IPL_SYMBOL_CONST_ITER iter(syms);

	for (const IPL_SYMBOL_NODE *s = iter.First(); 
	     k+dim < soe->Num_Vars() && !iter.Is_Empty();
	     ++k, s = iter.Next()) {
	  if (ale(i,k+dim)) {
	    if (Lin_Symb == NULL) Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST, _mem_pool);
	    Lin_Symb->Append(CXX_NEW(IPL_INTSYMB_NODE(s->Symbol,ale(i,k+dim)),_mem_pool));
	  }
	} 
      } else {
	for (k = 0; k < _nest_depth; ++k) 
	  _lcoeff[k] = -ale(i,dim+k);
	Const_Offset = ble[i];
    
	// Set the Lin_Symb list
	IPL_SYMBOL_CONST_ITER iter(syms);

	for (const IPL_SYMBOL_NODE *s = iter.First(); 
	     k+dim < soe->Num_Vars() && !iter.Is_Empty();
	     ++k, s = iter.Next()) {
	  if (ale(i,k+dim)) {
	    if (Lin_Symb == NULL) Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST, _mem_pool);
	    Lin_Symb->Append(CXX_NEW(IPL_INTSYMB_NODE(s->Symbol,-ale(i,k+dim)),_mem_pool));
	  }
	}
      }
    }

    break;

  }

}

#ifndef LNO

/*****************************************************************************
 *
 * Initialize static variables to be used by the access vector utils, if
 * invoked from outside of LNO
 *
 ****************************************************************************/

void Initialize_Access_Vals(DU_MANAGER* du_mgr, FILE *tfile)
{
    Du_Mgr = du_mgr;
    Set_Trace_File_internal(tfile);

    MEM_POOL_Initialize(&LNO_local_pool, "Access_Vector_Pool", FALSE);
    MEM_POOL_Push(&LNO_local_pool);
}


/*****************************************************************************
 *
 * Finalize variables, i.e. delete the mempool when done with the access
 * utils, if invoked from outside of LNO.
 *
 ****************************************************************************/

void Finalize_Access_Vals()
{
    MEM_POOL_Pop(&LNO_local_pool);
    MEM_POOL_Delete(&LNO_local_pool);

    // Just to be safe.
    Du_Mgr = NULL;
}

#endif

/** DAVID CODE BEGIN **/
#ifdef LNO

IPL_ACCESS_VECTOR* IPL_ACCESS_VECTOR::divide_by_const(UINT divisor, WN *wn)
{
    if (Too_Messy) return NULL;

    // quotient
    IPL_ACCESS_VECTOR *q = CXX_NEW(
            IPL_ACCESS_VECTOR(_nest_depth, _mem_pool), _mem_pool);
    // remainder (a copy of this vector)
    IPL_ACCESS_VECTOR *r = CXX_NEW(
            IPL_ACCESS_VECTOR(_nest_depth, _mem_pool), _mem_pool);

    /* Deal with the constant first. */
    q->Const_Offset = Const_Offset / divisor;
    r->Const_Offset = Const_Offset - q->Const_Offset * divisor;
    

    /* Go through the coefficients of loop index variables.
     * (coeff * iv) / divisor =
     *     (coeff / divisor) * iv + (coeff mod divisor)*iv
     *
     * TODO: there could be two choices if coeff is -ive, e.g. -6i / 4 = -1i
     * or -2i. We should try both.
     */
    if (_lcoeff != NULL)
    {
        q->_lcoeff = CXX_NEW_ARRAY(mINT32, _nest_depth, _mem_pool);
        r->_lcoeff = CXX_NEW_ARRAY(mINT32, _nest_depth, _mem_pool);
        for (INT i = 0; i < _nest_depth; ++i) {
            mINT32 q_lcoeff = _lcoeff[i] / divisor;
            mINT32 r_lcoeff = _lcoeff[i] - q_lcoeff * divisor;
            q->_lcoeff[i] = q_lcoeff;
            r->_lcoeff[i] = r_lcoeff;
        }
    }

    /* Go through the linear terms similarly. */

    if (Lin_Symb != NULL)
    {
        IPL_INTSYMB_ITER ii(Lin_Symb);
        for (IPL_INTSYMB_NODE* in = ii.First(); !ii.Is_Empty(); in = ii.Next()) 
        {
            INT32 q_coeff = in->Coeff / divisor;
            INT32 r_coeff = in->Coeff - q_coeff * divisor;
            if (q_coeff != 0) {
                if (q->Lin_Symb == NULL) {
                    q->Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST,_mem_pool);
                }
                q->Lin_Symb->Append(CXX_NEW(
                            IPL_INTSYMB_NODE(in->Symbol, q_coeff), _mem_pool));
            }
            if (r_coeff != 0) {
                if (r->Lin_Symb == NULL) {
                    r->Lin_Symb = CXX_NEW(IPL_INTSYMB_LIST,_mem_pool);
                }
                r->Lin_Symb->Append(CXX_NEW(
                            IPL_INTSYMB_NODE(in->Symbol, r_coeff), _mem_pool));
            }
        }
    }

    /* Go through the non-linear terms. */

    if (Non_Lin_Symb != NULL)
    {
        IPL_SUMPROD_ITER sli(Non_Lin_Symb);
        for (IPL_SUMPROD_NODE *sln = sli.First(); !sli.Is_Empty();
                sln = sli.Next())
        {
            INT32 q_coeff = sln->Coeff / divisor;
            INT32 r_coeff = sln->Coeff - q_coeff * divisor;
            if (q_coeff != 0) {
                if (q->Non_Lin_Symb == NULL) {
                    q->Non_Lin_Symb = CXX_NEW(IPL_SUMPROD_LIST,_mem_pool);
                }
                IPL_SUMPROD_NODE *q_sn = CXX_NEW(IPL_SUMPROD_NODE(
                            CXX_NEW(IPL_SYMBOL_LIST(sln->Prod_List, _mem_pool),
                                _mem_pool), q_coeff), _mem_pool);
                q->Non_Lin_Symb->Append(q_sn);
            }
            if (r_coeff != 0) {
                if (r->Non_Lin_Symb == NULL) {
                    r->Non_Lin_Symb = CXX_NEW(IPL_SUMPROD_LIST,_mem_pool);
                }
                IPL_SUMPROD_NODE *r_sn = CXX_NEW(IPL_SUMPROD_NODE(
                            CXX_NEW(IPL_SYMBOL_LIST(sln->Prod_List, _mem_pool),
                                _mem_pool), r_coeff), _mem_pool);
                r->Non_Lin_Symb->Append(r_sn);
            }
        }
    }

    q->Too_Messy = r->Too_Messy = FALSE;

    /* Prove that the remainder is in the range of 0 .. (divisor-1). */

    // Is it possible that r < 0 (r + 1 <= 0)?
    r->Const_Offset = -(r->Const_Offset+1);
    if (!Is_Consistent_Condition(r, wn)) {
        // Is it possible that r >= divisor (divisor - r <= 0)?
        r->Const_Offset++;              // r <= 0
        r->Const_Offset += divisor;     // r - divisor <= 0
        r->Negate_Me();                 // divisor - r <= 0
        if (!Is_Consistent_Condition(r, wn)) {
            // Recover the original remainder.
            r->Negate_Me();
            r->Const_Offset -= divisor;
            r->Const_Offset = -r->Const_Offset;
            // Shallow-copy the remainder to this vector.
            Const_Offset = r->Const_Offset;
            _lcoeff = r->_lcoeff;
            Lin_Symb = r->Lin_Symb;
            Non_Lin_Symb = r->Non_Lin_Symb;
            // TODO: do we need to DELETE the original data structs?
            return q;
        }
    }

    // The division is not successful, so delete r and q.
    // TODO: do we need MEM_POOL_Set_Default?
    CXX_DELETE(q, _mem_pool);
    CXX_DELETE(r, _mem_pool);

    return NULL;
}

BOOL IPL_ACCESS_VECTOR::perfect_divide_by_const(UINT divisor)
{
    if (Too_Messy) return FALSE;

    /* Check the constant and the coeffients of IVs, Lin_Symb's and
     * Non_Lin_Symb's.
     */
    if (Const_Offset % divisor != 0) return FALSE;
    if (_lcoeff != NULL) {
        for (INT i = 0; i < _nest_depth; ++i) {
            if (_lcoeff[i] % divisor != 0) return FALSE;
        }
    }
    if (Lin_Symb != NULL) {
        IPL_INTSYMB_ITER ii(Lin_Symb);
        for (IPL_INTSYMB_NODE* in = ii.First(); !ii.Is_Empty(); in = ii.Next()) {
            if (in->Coeff % divisor != 0) return FALSE;
        }
    }
    if (Non_Lin_Symb != NULL) {
        IPL_SUMPROD_ITER sli(Non_Lin_Symb);
        for (IPL_SUMPROD_NODE *sln = sli.First(); !sli.Is_Empty();
                sln = sli.Next()) {
            if (sln->Coeff % divisor != 0) return FALSE;
        }
    }

    /* Do the actual division on the spot. */
    Const_Offset /= divisor;
    if (_lcoeff != NULL) {
        for (INT i = 0; i < _nest_depth; ++i) _lcoeff[i] /= divisor;
    }
    if (Lin_Symb != NULL) {
        IPL_INTSYMB_ITER ii(Lin_Symb);
        for (IPL_INTSYMB_NODE* in = ii.First(); !ii.Is_Empty(); in = ii.Next()) {
            in->Coeff /= divisor;
        }
    }
    if (Non_Lin_Symb != NULL) {
        IPL_SUMPROD_ITER sli(Non_Lin_Symb);
        for (IPL_SUMPROD_NODE *sln = sli.First(); !sli.Is_Empty();
                sln = sli.Next()) {
            sln->Coeff /= divisor;
        }
    }

    return TRUE;
}

WN* IPL_SYMBOL::to_wn()
{
    Is_True(!_is_formal, ("IPL_SYMBOL::to_wn: meet a formal!\n"));

    ST *st = St();
    TY_IDX ty_idx = ST_type(st);

    return WN_Ldid(TY_mtype(ty_idx), WN_Offset(), ST_st_idx(st), ty_idx, 0);
}

WN* IPL_INTSYMB_NODE::to_wn()
{
    TYPE_ID rtype = Symbol.Type;
    return WN_Mpy(rtype, WN_Intconst(rtype, Coeff), Symbol.to_wn());
}

WN* IPL_SUMPROD_NODE::to_wn()
{
    WN *wn = WN_Intconst(Integer_type, Coeff);

    IPL_SYMBOL_ITER si(Prod_List);
    for (IPL_SYMBOL_NODE *sn = si.First(); !si.Is_Empty(); sn = si.Next()) {
        wn = WN_Mpy(Integer_type, wn, sn->Symbol.to_wn());
    }

    return wn;
}

WN* IPL_ACCESS_VECTOR::to_wn(DOLOOP_STACK *stack)
{
    // Start from the constant offset.
    WN *wn = WN_Intconst(Integer_type, Const_Offset);

    // Add the linear terms for loop index variables.
    if (_lcoeff != NULL) {
        for (INT i = 0; i < _nest_depth; ++i) {
            IPL_SYMBOL dl_sym(WN_index(stack->Bottom_nth(i)));
            wn = WN_Add(Integer_type, wn,
                    WN_Mpy(Integer_type, dl_sym.to_wn(),
                        WN_Intconst(Integer_type, _lcoeff[i])));
        }
    }

    // Add the symbolic linear terms.
    if (Lin_Symb != NULL) {
        IPL_INTSYMB_ITER ii(Lin_Symb);
        for (IPL_INTSYMB_NODE* in = ii.First(); !ii.Is_Empty(); in = ii.Next()) {
            wn = WN_Add(Integer_type, wn, in->to_wn());
        }
    }

    // Add non-linear terms.
    if (Non_Lin_Symb != NULL) {
        IPL_SUMPROD_ITER sli(Non_Lin_Symb);
        for (IPL_SUMPROD_NODE *sln = sli.First(); !sli.Is_Empty();
                sln = sli.Next()) {
            wn = WN_Add(Integer_type, wn, sln->to_wn());
        }
    }

    return wn;
}

#endif  // LNO
/*** DAVID CODE END ***/

/*** DAVID CODE END ***/
