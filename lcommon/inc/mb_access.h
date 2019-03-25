
/*!
 *************************************************************************************
 * \file mb_access.h
 *
 * \brief
 *    Functions for macroblock neighborhoods
 *
 * \author
 *     Main contributors (see contributors.h for copyright, address and affiliation details)
 *     - Karsten S�hring                 <suehring@hhi.de> 
 *     - Alexis Michael Tourapis         <alexismt@ieee.org>  
 *************************************************************************************
 */

#ifndef _MB_ACCESS_H_
#define _MB_ACCESS_H_

extern void CheckAvailabilityOfNeighbors(Macroblock *currMB);

extern void getAffNeighbour         (Macroblock *currMB, int xN, int yN, int mb_size[2], PixelPos *pix);
extern void getNonAffNeighbour      (Macroblock *currMB, int xN, int yN, int mb_size[2], PixelPos *pix);
extern void get4x4Neighbour         (Macroblock *currMB, int xN, int yN, int mb_size[2], PixelPos *pix);
extern Boolean mb_is_available      (int mbAddr, Macroblock *currMB);
extern void get_mb_pos              (ImageParameters *p_Img, int mb_addr, int mb_size[2], short *x, short *y);
extern void get_mb_block_pos_normal (int mb_addr, short *x, short *y);
extern void get_mb_block_pos_mbaff  (int mb_addr, short *x, short *y);


#endif