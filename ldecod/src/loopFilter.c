
/*!
 *************************************************************************************
 * \file loopFilter.c
 *
 * \brief
 *    Filter to reduce blocking artifacts on a macroblock level.
 *    The filter strength is QP dependent.
 *
 * \author
 *    Contributors:
 *    - Peter List       Peter.List@t-systems.de:  Original code                                 (13-Aug-2001)
 *    - Jani Lainema     Jani.Lainema@nokia.com:   Some bug fixing, removal of recusiveness      (16-Aug-2001)
 *    - Peter List       Peter.List@t-systems.de:  inplace filtering and various simplifications (10-Jan-2002)
 *    - Anthony Joch     anthony@ubvideo.com:      Simplified switching between filters and 
 *                                                 non-recursive default filter.                 (08-Jul-2002)
 *    - Cristina Gomila  cristina.gomila@thomson.net: Simplification of the chroma deblocking
 *                                                    from JVT-E089                              (21-Nov-2002)
 *************************************************************************************
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "global.h"
#include "image.h"
#include "mb_access.h"
#include "loopfilter.h"

extern const byte QP_SCALE_CR[52] ;

byte mixedModeEdgeFlag, fieldModeFilteringFlag;

/*********************************************************************************************************/

#define  IClip( Min, Max, Val) (((Val)<(Min))? (Min):(((Val)>(Max))? (Max):(Val)))

// NOTE: to change the tables below for instance when the QP doubling is changed from 6 to 8 values 
//       send an e-mail to Peter.List@t-systems.com to get a little program that calculates them automatically 

byte ALPHA_TABLE[52]  = {0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,4,4,5,6,  7,8,9,10,12,13,15,17,  20,22,25,28,32,36,40,45,  50,56,63,71,80,90,101,113,  127,144,162,182,203,226,255,255} ;
byte  BETA_TABLE[52]  = {0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,2,2,2,3,  3,3,3, 4, 4, 4, 6, 6,   7, 7, 8, 8, 9, 9,10,10,  11,11,12,12,13,13, 14, 14,   15, 15, 16, 16, 17, 17, 18, 18} ;
byte CLIP_TAB[52][5]  =
{
  { 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},
  { 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},{ 0, 0, 0, 0, 0},
  { 0, 0, 0, 0, 0},{ 0, 0, 0, 1, 1},{ 0, 0, 0, 1, 1},{ 0, 0, 0, 1, 1},{ 0, 0, 0, 1, 1},{ 0, 0, 1, 1, 1},{ 0, 0, 1, 1, 1},{ 0, 1, 1, 1, 1},
  { 0, 1, 1, 1, 1},{ 0, 1, 1, 1, 1},{ 0, 1, 1, 1, 1},{ 0, 1, 1, 2, 2},{ 0, 1, 1, 2, 2},{ 0, 1, 1, 2, 2},{ 0, 1, 1, 2, 2},{ 0, 1, 2, 3, 3},
  { 0, 1, 2, 3, 3},{ 0, 2, 2, 3, 3},{ 0, 2, 2, 4, 4},{ 0, 2, 3, 4, 4},{ 0, 2, 3, 4, 4},{ 0, 3, 3, 5, 5},{ 0, 3, 4, 6, 6},{ 0, 3, 4, 6, 6},
  { 0, 4, 5, 7, 7},{ 0, 4, 5, 8, 8},{ 0, 4, 6, 9, 9},{ 0, 5, 7,10,10},{ 0, 6, 8,11,11},{ 0, 6, 8,13,13},{ 0, 7,10,14,14},{ 0, 8,11,16,16},
  { 0, 9,12,18,18},{ 0,10,13,20,20},{ 0,11,15,23,23},{ 0,13,17,25,25}
} ;

char chroma_edge[2][4][4] = //[dir][edge][yuv_format]
{ { {-1, 0, 0, 0},
    {-1,-1,-1, 1},
    {-1, 1, 1, 2},
    {-1,-1,-1, 3}},

  { {-1, 0, 0, 0},
    {-1,-1, 1, 1},
    {-1, 1, 2, 2},
    {-1,-1, 3, 3}}};

void GetStrength(byte Strength[16],struct img_par *img,int MbQAddr,int dir,int edge, int mvlimit,StorablePicture *p);
void EdgeLoop(imgpel** Img, byte Strength[16],struct img_par *img, int MbQAddr, int AlphaC0Offset, int BetaOffset, int dir, int edge, int width, int yuv, int uv, StorablePicture *p);
void DeblockMb(ImageParameters *img, StorablePicture *p, int MbQAddr) ;

/*!
 *****************************************************************************************
 * \brief
 *    Filter all macroblocks in order of increasing macroblock address.
 *****************************************************************************************
 */
void DeblockPicture(ImageParameters *img, StorablePicture *p)
{
  unsigned i;

  for (i=0; i<p->PicSizeInMbs; i++)
  {
    DeblockMb( img, p, i ) ;
  }
} 


/*!
 *****************************************************************************************
 * \brief
 *    Deblocking filter for one macroblock.
 *****************************************************************************************
 */

void DeblockMb(ImageParameters *img, StorablePicture *p, int MbQAddr)
{
  int           EdgeCondition;
  int           dir,edge;
  byte          Strength[16];
  int           mb_x, mb_y;

  int           filterNon8x8LumaEdgesFlag[4] = {1,1,1,1};
  int           filterLeftMbEdgeFlag;
  int           filterTopMbEdgeFlag;
  int           fieldModeMbFlag;
  int           mvlimit=4;
  int           i, StrengthSum;
  Macroblock    *MbQ;
  imgpel **imgY   = p->imgY;
  imgpel ***imgUV = p->imgUV;

  int           edge_cr;
  
  
  img->DeblockCall = 1;
  get_mb_pos (MbQAddr, &mb_x, &mb_y);
  filterLeftMbEdgeFlag  = (mb_x != 0);
  filterTopMbEdgeFlag   = (mb_y != 0);

  MbQ  = &(img->mb_data[MbQAddr]) ; // current Mb

  if (MbQ->mb_type == I8MB)
    assert(MbQ->luma_transform_size_8x8_flag);

  filterNon8x8LumaEdgesFlag[1] = 
  filterNon8x8LumaEdgesFlag[3] = !(MbQ->luma_transform_size_8x8_flag);
    
  if (p->MbaffFrameFlag && mb_y==16 && MbQ->mb_field)
    filterTopMbEdgeFlag = 0;

  fieldModeMbFlag       = (p->structure!=FRAME) || (p->MbaffFrameFlag && MbQ->mb_field);
  if (fieldModeMbFlag)
    mvlimit = 2;

  // return, if filter is disabled
  if (MbQ->LFDisableIdc==1) {
    img->DeblockCall = 0;
    return;
  }

  if (MbQ->LFDisableIdc==2)
  {
    // don't filter at slice boundaries
    filterLeftMbEdgeFlag = MbQ->mbAvailA;
    // if this the bottom of a frame macroblock pair then always filter the top edge
    if (p->MbaffFrameFlag && !MbQ->mb_field && (MbQAddr % 2)) filterTopMbEdgeFlag  = 1;
    else                                                      filterTopMbEdgeFlag  = MbQ->mbAvailB;
  }

  img->current_mb_nr = MbQAddr;
  CheckAvailabilityOfNeighbors();

  for( dir=0 ; dir<2 ; dir++ )                                             // vertical edges, than horicontal edges
  {
    EdgeCondition = (dir && filterTopMbEdgeFlag) || (!dir && filterLeftMbEdgeFlag); // can not filter beyond picture boundaries
    for( edge=0 ; edge<4 ; edge++ )                                            // first 4 vertical strips of 16 pel
    {                                                                                         // then  4 horicontal
      if( edge || EdgeCondition )
      {
        edge_cr = chroma_edge[dir][edge][p->chroma_format_idc];
        
        GetStrength(Strength,img,MbQAddr,dir,edge, mvlimit, p); // Strength for 4 blks in 1 stripe
        StrengthSum = Strength[0];
        for (i = 1; i < 16; i++) StrengthSum += Strength[i];
        if( StrengthSum )                      // only if one of the 16 Strength bytes is != 0
        {
          if (filterNon8x8LumaEdgesFlag[edge])
            EdgeLoop( imgY, Strength, img, MbQAddr, MbQ->LFAlphaC0Offset, MbQ->LFBetaOffset, dir, edge, p->size_x, 0, 0, p) ; 
          if( (imgUV != NULL) && (edge_cr >= 0))
          {
            EdgeLoop( imgUV[0], Strength, img, MbQAddr, MbQ->LFAlphaC0Offset, MbQ->LFBetaOffset, dir, edge_cr, p->size_x_cr, 1, 0, p) ; 
            EdgeLoop( imgUV[1], Strength, img, MbQAddr, MbQ->LFAlphaC0Offset, MbQ->LFBetaOffset, dir, edge_cr, p->size_x_cr, 1, 1, p) ; 
          }
        }

        if (dir && !edge && !MbQ->mb_field && mixedModeEdgeFlag) {
          // this is the extra horizontal edge between a frame macroblock pair and a field above it
          img->DeblockCall = 2;
          GetStrength(Strength,img,MbQAddr,dir,4, mvlimit, p); // Strength for 4 blks in 1 stripe
          if( *((int*)Strength) )                      // only if one of the 4 Strength bytes is != 0
          {
            if (filterNon8x8LumaEdgesFlag[edge])
              EdgeLoop( imgY, Strength, img, MbQAddr, MbQ->LFAlphaC0Offset, MbQ->LFBetaOffset, dir, 4, p->size_x, 0, 0, p) ; 
            if( (imgUV != NULL) && (edge_cr >= 0))
            {
              EdgeLoop( imgUV[0], Strength, img, MbQAddr, MbQ->LFAlphaC0Offset, MbQ->LFBetaOffset, dir, 4, p->size_x_cr, 1, 0, p) ; 
              EdgeLoop( imgUV[1], Strength, img, MbQAddr, MbQ->LFAlphaC0Offset, MbQ->LFBetaOffset, dir, 4, p->size_x_cr, 1, 1, p) ; 
            }
          }
          img->DeblockCall = 1;
        }

      }
    }//end edge
  }//end loop dir
  img->DeblockCall = 0;

}

  /*!
 *********************************************************************************************
 * \brief
 *    returns a buffer of 16 Strength values for one stripe in a mb (for different Frame types)
 *********************************************************************************************
 */

int  ININT_STRENGTH[4] = {0x04040404, 0x03030303, 0x03030303, 0x03030303} ; 
byte BLK_NUM[2][4][4]  = {{{0,4,8,12},{1,5,9,13},{2,6,10,14},{3,7,11,15}},{{0,1,2,3},{4,5,6,7},{8,9,10,11},{12,13,14,15}}} ;
byte BLK_4_TO_8[16]    = {0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3} ;
#define ANY_INTRA (MbP->mb_type==I4MB||MbP->mb_type==I16MB||MbP->mb_type==IPCM||MbQ->mb_type==I4MB||MbQ->mb_type==I16MB||MbQ->mb_type==IPCM)

void GetStrength(byte Strength[16],struct img_par *img,int MbQAddr,int dir,int edge, int mvlimit, StorablePicture *p)
{
  int    blkP, blkQ, idx;
  int    blk_x, blk_x2, blk_y, blk_y2 ;
  int    ***list0_mv = p->mv[LIST_0];
  int    ***list1_mv = p->mv[LIST_1];
  int    **list0_refIdxArr = p->ref_idx[LIST_0];
  int    **list1_refIdxArr = p->ref_idx[LIST_1];
  int64    **list0_refPicIdArr = p->ref_pic_id[LIST_0];
  int64    **list1_refPicIdArr = p->ref_pic_id[LIST_1];
  int    xQ, xP, yQ, yP;
  int    mb_x, mb_y;
  Macroblock    *MbQ;
  Macroblock    *MbP;
  PixelPos pixP;

  MbQ = &(img->mb_data[MbQAddr]);

  for( idx=0 ; idx<16 ; idx++ )
  {                                                                
    xQ = dir ? idx : edge << 2;
    yQ = dir ? (edge < 4 ? edge << 2 : 1) : idx;
    getNeighbour(MbQAddr, xQ - (1 - dir), yQ - dir, 1, &pixP);
    xP = pixP.x;
    yP = pixP.y;
    MbP = &(img->mb_data[pixP.mb_addr]);
    mixedModeEdgeFlag = MbQ->mb_field != MbP->mb_field;

    blkQ = ((yQ>>2)<<2) + (xQ>>2);
    blkP = ((yP>>2)<<2) + (xP>>2);

    if ((p->slice_type==SP_SLICE)||(p->slice_type==SI_SLICE) )
    {
      Strength[idx] = (edge == 0 && (((!p->MbaffFrameFlag && (p->structure==FRAME)) ||
      (p->MbaffFrameFlag && !MbP->mb_field && !MbQ->mb_field)) ||
      ((p->MbaffFrameFlag || (p->structure != FRAME)) && !dir))) ? 4 : 3;
    }
    else
    {
      // Start with Strength=3. or Strength=4 for Mb-edge
      Strength[idx] = (edge == 0 && (((!p->MbaffFrameFlag && (p->structure==FRAME)) ||
        (p->MbaffFrameFlag && !MbP->mb_field && !MbQ->mb_field)) ||
        ((p->MbaffFrameFlag || (p->structure!=FRAME)) && !dir))) ? 4 : 3;

      if(  !(MbP->mb_type==I4MB || MbP->mb_type==I16MB || MbP->mb_type==I8MB || MbP->mb_type==IPCM)
        && !(MbQ->mb_type==I4MB || MbQ->mb_type==I16MB || MbQ->mb_type==I8MB || MbQ->mb_type==IPCM) )
      {
        if( ((MbQ->cbp_blk &  (1 << blkQ )) != 0) || ((MbP->cbp_blk &  (1 << blkP)) != 0) )
          Strength[idx] = 2 ;
        else
        {                                                     // if no coefs, but vector difference >= 1 set Strength=1 
          // if this is a mixed mode edge then one set of reference pictures will be frame and the
          // other will be field
          if (mixedModeEdgeFlag)
          {
            (Strength[idx] = 1);
          }
          else
          {
            get_mb_block_pos (MbQAddr, &mb_x, &mb_y);
            blk_y  = (mb_y<<2) + (blkQ >> 2) ;
            blk_x  = (mb_x<<2) + (blkQ  & 3) ;
            blk_y2 = pixP.pos_y >> 2;
            blk_x2 = pixP.pos_x >> 2;
            {
              int64 ref_p0,ref_p1,ref_q0,ref_q1;      
              ref_p0 = list0_refIdxArr[blk_x][blk_y]<0 ? -1 : list0_refPicIdArr[blk_x][blk_y];
              ref_q0 = list0_refIdxArr[blk_x2][blk_y2]<0 ? -1 : list0_refPicIdArr[blk_x2][blk_y2];
              ref_p1 = list1_refIdxArr[blk_x][blk_y]<0 ? -1 : list1_refPicIdArr[blk_x][blk_y];
              ref_q1 = list1_refIdxArr[blk_x2][blk_y2]<0 ? -1 : list1_refPicIdArr[blk_x2][blk_y2];
              if ( ((ref_p0==ref_q0) && (ref_p1==ref_q1)) ||
                ((ref_p0==ref_q1) && (ref_p1==ref_q0))) 
              {
                Strength[idx]=0;
                // L0 and L1 reference pictures of p0 are different; q0 as well
                if (ref_p0 != ref_p1) 
                { 
                  // compare MV for the same reference picture
                  if (ref_p0==ref_q0) 
                  {
                    Strength[idx] =  (abs( list0_mv[blk_x][blk_y][0] - list0_mv[blk_x2][blk_y2][0]) >= 4) |
                      (abs( list0_mv[blk_x][blk_y][1] - list0_mv[blk_x2][blk_y2][1]) >= mvlimit) |
                      (abs( list1_mv[blk_x][blk_y][0] - list1_mv[blk_x2][blk_y2][0]) >= 4) |
                      (abs( list1_mv[blk_x][blk_y][1] - list1_mv[blk_x2][blk_y2][1]) >= mvlimit);
                  }
                  else 
                  {
                    Strength[idx] =  (abs( list0_mv[blk_x][blk_y][0] - list1_mv[blk_x2][blk_y2][0]) >= 4) |
                      (abs( list0_mv[blk_x][blk_y][1] - list1_mv[blk_x2][blk_y2][1]) >= mvlimit) |
                      (abs( list1_mv[blk_x][blk_y][0] - list0_mv[blk_x2][blk_y2][0]) >= 4) |
                      (abs( list1_mv[blk_x][blk_y][1] - list0_mv[blk_x2][blk_y2][1]) >= mvlimit);
                  } 
                }
                else 
                { // L0 and L1 reference pictures of p0 are the same; q0 as well
                
                  Strength[idx] =  ((abs( list0_mv[blk_x][blk_y][0] - list0_mv[blk_x2][blk_y2][0]) >= 4) |
                    (abs( list0_mv[blk_x][blk_y][1] - list0_mv[blk_x2][blk_y2][1]) >= mvlimit ) |
                    (abs( list1_mv[blk_x][blk_y][0] - list1_mv[blk_x2][blk_y2][0]) >= 4) |
                    (abs( list1_mv[blk_x][blk_y][1] - list1_mv[blk_x2][blk_y2][1]) >= mvlimit))
                    &&
                    ((abs( list0_mv[blk_x][blk_y][0] - list1_mv[blk_x2][blk_y2][0]) >= 4) |
                    (abs( list0_mv[blk_x][blk_y][1] - list1_mv[blk_x2][blk_y2][1]) >= mvlimit) |
                    (abs( list1_mv[blk_x][blk_y][0] - list0_mv[blk_x2][blk_y2][0]) >= 4) |
                    (abs( list1_mv[blk_x][blk_y][1] - list0_mv[blk_x2][blk_y2][1]) >= mvlimit));
                }       
              }
              else 
              {
                Strength[idx] = 1;        
              } 
            }
          }
        }
      }
    }
  }
}

#define CQPOF(qp, uv) (Clip3(0, 51, qp + p->chroma_qp_offset[uv]))

/*!
 *****************************************************************************************
 * \brief
 *    Filters one edge of 16 (luma) or 8 (chroma) pel
 *****************************************************************************************
 */
void EdgeLoop(imgpel** Img, byte Strength[16],struct img_par *img, int MbQAddr, int AlphaC0Offset, int BetaOffset,
              int dir, int edge, int width, int yuv, int uv, StorablePicture *p)
{
  int      pel, ap = 0, aq = 0, Strng ;
  int      incP, incQ;
  int      C0, c0, Delta, dif, AbsDelta ;
  int      L2 = 0, L1, L0, R0, R1, R2 = 0, RL0, L3, R3 ;
  int      Alpha = 0, Beta = 0 ;
  byte*    ClipTab = NULL;   
  int      small_gap;
  int      indexA, indexB;
  int      PelNum;
  int      StrengthIdx;
  imgpel   *SrcPtrP, *SrcPtrQ;
  int      QP;
  int      xP, xQ, yP, yQ;
  Macroblock *MbQ, *MbP;
  PixelPos pixP, pixQ;
  int      bitdepth_scale; 
  int      pelnum_cr[2][4] =  {{0,8,16,16}, {0,8, 8,16}};  //[dir:0=vert, 1=hor.][yuv_format]
  
  if (!yuv)
    bitdepth_scale = 1<<(img->bitdepth_luma - 8);
  else
    bitdepth_scale = 1<<(img->bitdepth_chroma - 8);
  
  PelNum = yuv ? pelnum_cr[dir][p->chroma_format_idc] : 16 ;

  for( pel=0 ; pel<PelNum ; pel++ )
  {
    xQ = dir ? pel : edge << 2;
    yQ = dir ? (edge < 4 ? edge << 2 : 1) : pel;
    getNeighbour(MbQAddr, xQ, yQ, 1-yuv, &pixQ);
    getNeighbour(MbQAddr, xQ - (1 - dir), yQ - dir, 1-yuv, &pixP);
    xP = pixP.x;
    yP = pixP.y;
    MbQ = &(img->mb_data[MbQAddr]);
    MbP = &(img->mb_data[pixP.mb_addr]);
    fieldModeFilteringFlag = MbQ->mb_field || MbP->mb_field;
    StrengthIdx = (yuv&&(PelNum==8)) ? ((MbQ->mb_field && !MbP->mb_field) ? pel<<1 :((pel>>1)<<2)+(pel%2)) : pel ;
    
    if (pixP.available || (MbQ->LFDisableIdc== 0)) 
    {
      incQ = dir ? ((fieldModeFilteringFlag && !MbQ->mb_field) ? 2 * width : width) : 1;
      incP = dir ? ((fieldModeFilteringFlag && !MbP->mb_field) ? 2 * width : width) : 1;
      SrcPtrQ = &(Img[pixQ.pos_y][pixQ.pos_x]);
      SrcPtrP = &(Img[pixP.pos_y][pixP.pos_x]);

      // Average QP of the two blocks
      QP  = yuv ? (QP_SCALE_CR[CQPOF(MbP->qp,uv)] + QP_SCALE_CR[CQPOF(MbQ->qp,uv)] + 1) >> 1 : (MbP->qp + MbQ->qp + 1) >> 1;

      indexA = IClip(0, MAX_QP, QP + AlphaC0Offset);
      indexB = IClip(0, MAX_QP, QP + BetaOffset);
    
      Alpha  =ALPHA_TABLE[indexA] * bitdepth_scale;
      Beta   =BETA_TABLE[indexB]  * bitdepth_scale;
      ClipTab=CLIP_TAB[indexA];

      L0  = SrcPtrP[0] ;
      R0  = SrcPtrQ[0] ;
      L1  = SrcPtrP[-incP] ;
      R1  = SrcPtrQ[ incQ] ;
      L2  = SrcPtrP[-incP*2] ;
      R2  = SrcPtrQ[ incQ*2] ;
      L3  = SrcPtrP[-incP*3] ;
      R3  = SrcPtrQ[ incQ*3] ;

      if( (Strng = Strength[StrengthIdx]) )
      {
        AbsDelta  = abs( Delta = R0 - L0 )  ;
      
        if( AbsDelta < Alpha )
        {
          C0  = ClipTab[ Strng ] * bitdepth_scale;
          if( ((abs( R0 - R1) - Beta )  & (abs(L0 - L1) - Beta )) < 0  ) 
          {
            if( !yuv)
            {
              aq  = (abs( R0 - R2) - Beta ) < 0  ;
              ap  = (abs( L0 - L2) - Beta ) < 0  ;
            }
          
            RL0             = L0 + R0 ;
          
            if(Strng == 4 )    // INTRA strong filtering
            {
              if( yuv)  // Chroma
              {
                SrcPtrQ[0] = ((R1 << 1) + R0 + L1 + 2) >> 2; 
                SrcPtrP[0] = ((L1 << 1) + L0 + R1 + 2) >> 2;                                           
              }
              else  // Luma
              {
                small_gap = (AbsDelta < ((Alpha >> 2) + 2));
              
                aq &= small_gap;
                ap &= small_gap;
              
                SrcPtrQ[0]   = aq ? ( L1 + ((R1 + RL0) << 1) +  R2 + 4) >> 3 : ((R1 << 1) + R0 + L1 + 2) >> 2 ;
                SrcPtrP[0]   = ap ? ( R1 + ((L1 + RL0) << 1) +  L2 + 4) >> 3 : ((L1 << 1) + L0 + R1 + 2) >> 2 ;
              
                SrcPtrQ[ incQ] =   aq  ? ( R2 + R0 + R1 + L0 + 2) >> 2 : R1;
                SrcPtrP[-incP] =   ap  ? ( L2 + L1 + L0 + R0 + 2) >> 2 : L1;
              
                SrcPtrQ[ incQ*2] = aq ? (((R3 + R2) <<1) + R2 + R1 + RL0 + 4) >> 3 : R2;
                SrcPtrP[-incP*2] = ap ? (((L3 + L2) <<1) + L2 + L1 + RL0 + 4) >> 3 : L2;
              }
            }
            else                                                     // normal filtering
            {
              c0               = yuv? (C0+1):(C0 + ap + aq) ;
              dif              = IClip( -c0, c0, ( (Delta << 2) + (L1 - R1) + 4) >> 3 ) ;
              if(!yuv)
              {
                SrcPtrP[0]  = IClip(0, img->max_imgpel_value, L0 + dif) ;
                SrcPtrQ[0]  = IClip(0, img->max_imgpel_value, R0 - dif) ;
              } 
              else 
              {
                SrcPtrP[0]  = IClip(0, img->max_imgpel_value_uv, L0 + dif) ;
                SrcPtrQ[0]  = IClip(0, img->max_imgpel_value_uv, R0 - dif) ;
              }
            
              if( !yuv )
              {
                if( ap )
                  SrcPtrP[-incP] += IClip( -C0,  C0, ( L2 + ((RL0 + 1) >> 1) - (L1<<1)) >> 1 ) ;
                if( aq  )
                  SrcPtrQ[ incQ] += IClip( -C0,  C0, ( R2 + ((RL0 + 1) >> 1) - (R1<<1)) >> 1 ) ;
              } ;
            } ;
          } ; 
        } ;
      } ;
    } ;
  }
}

