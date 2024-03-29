/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2016, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/** \file     TComRom.cpp
    \brief    global variables & functions
*/
#include "TComRom.h"
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <iomanip>
#include <assert.h>
#include "TComDataCU.h"
#include "Debug.h"
// ====================================================================================================================
// Initialize / destroy functions
// ====================================================================================================================
//! \ingroup TLibCommon
//! \{
const TChar* nalUnitTypeToString(NalUnitType type)
{
  switch (type)
  {
  case NAL_UNIT_CODED_SLICE_TRAIL_R:    return "TRAIL_R";
  case NAL_UNIT_CODED_SLICE_TRAIL_N:    return "TRAIL_N";
  case NAL_UNIT_CODED_SLICE_TSA_R:      return "TSA_R";
  case NAL_UNIT_CODED_SLICE_TSA_N:      return "TSA_N";
  case NAL_UNIT_CODED_SLICE_STSA_R:     return "STSA_R";
  case NAL_UNIT_CODED_SLICE_STSA_N:     return "STSA_N";
  case NAL_UNIT_CODED_SLICE_BLA_W_LP:   return "BLA_W_LP";
  case NAL_UNIT_CODED_SLICE_BLA_W_RADL: return "BLA_W_RADL";
  case NAL_UNIT_CODED_SLICE_BLA_N_LP:   return "BLA_N_LP";
  case NAL_UNIT_CODED_SLICE_IDR_W_RADL: return "IDR_W_RADL";
  case NAL_UNIT_CODED_SLICE_IDR_N_LP:   return "IDR_N_LP";
  case NAL_UNIT_CODED_SLICE_CRA:        return "CRA";
  case NAL_UNIT_CODED_SLICE_RADL_R:     return "RADL_R";
  case NAL_UNIT_CODED_SLICE_RADL_N:     return "RADL_N";
  case NAL_UNIT_CODED_SLICE_RASL_R:     return "RASL_R";
  case NAL_UNIT_CODED_SLICE_RASL_N:     return "RASL_N";
  case NAL_UNIT_VPS:                    return "VPS";
  case NAL_UNIT_SPS:                    return "SPS";
  case NAL_UNIT_PPS:                    return "PPS";
  case NAL_UNIT_ACCESS_UNIT_DELIMITER:  return "AUD";
  case NAL_UNIT_EOS:                    return "EOS";
  case NAL_UNIT_EOB:                    return "EOB";
  case NAL_UNIT_FILLER_DATA:            return "FILLER";
  case NAL_UNIT_PREFIX_SEI:             return "Prefix SEI";
  case NAL_UNIT_SUFFIX_SEI:             return "Suffix SEI";
  default:                              return "UNK";
  }
}
class ScanGenerator
{
private:
  UInt m_line, m_column;
  const UInt m_blockWidth, m_blockHeight;
  const UInt m_stride;
  const COEFF_SCAN_TYPE m_scanType;
public:
  ScanGenerator(UInt blockWidth, UInt blockHeight, UInt stride, COEFF_SCAN_TYPE scanType)
    : m_line(0), m_column(0), m_blockWidth(blockWidth), m_blockHeight(blockHeight), m_stride(stride), m_scanType(scanType)
  { }
  UInt GetCurrentX() const { return m_column; }
  UInt GetCurrentY() const { return m_line; }
  UInt GetNextIndex(UInt blockOffsetX, UInt blockOffsetY)
  {
    Int rtn=((m_line + blockOffsetY) * m_stride) + m_column + blockOffsetX;
    //advance line and column to the next position
    switch (m_scanType)
    {
      //------------------------------------------------
      case SCAN_DIAG:
        {
          if ((m_column == (m_blockWidth - 1)) || (m_line == 0)) //if we reach the end of a rank, go diagonally down to the next one
          {
            m_line   += m_column + 1;
            m_column  = 0;
            if (m_line >= m_blockHeight) //if that takes us outside the block, adjust so that we are back on the bottom row
            {
              m_column += m_line - (m_blockHeight - 1);
              m_line    = m_blockHeight - 1;
            }
          }
          else
          {
            m_column++;
            m_line--;
          }
        }
        break;
      //------------------------------------------------
      case SCAN_HOR:
        {
          if (m_column == (m_blockWidth - 1))
          {
            m_line++;
            m_column = 0;
          }
          else
          {
            m_column++;
          }
        }
        break;
      //------------------------------------------------
      case SCAN_VER:
        {
          if (m_line == (m_blockHeight - 1))
          {
            m_column++;
            m_line = 0;
          }
          else
          {
            m_line++;
          }
        }
        break;
      //------------------------------------------------
      default:
        {
          std::cerr << "ERROR: Unknown scan type \"" << m_scanType << "\"in ScanGenerator::GetNextIndex" << std::endl;
          exit(1);
        }
        break;
    }
    return rtn;
  }
};
// initialize ROM variables
Void initROM()
{
  Int i, c;
  // g_aucConvertToBit[ x ]: log2(x/4), if x=4 -> 0, x=8 -> 1, x=16 -> 2, ...
  ::memset( g_aucConvertToBit,   -1, sizeof( g_aucConvertToBit ) );
  c=0;
  for ( i=4; i<=MAX_CU_SIZE; i*=2 )
  {
    g_aucConvertToBit[ i ] = c;
    c++;
  }
  // initialise scan orders
  for(UInt log2BlockHeight = 0; log2BlockHeight < MAX_CU_DEPTH; log2BlockHeight++)
  {
    for(UInt log2BlockWidth = 0; log2BlockWidth < MAX_CU_DEPTH; log2BlockWidth++)
    {
      const UInt blockWidth  = 1 << log2BlockWidth;
      const UInt blockHeight = 1 << log2BlockHeight;
      const UInt totalValues = blockWidth * blockHeight;
      //--------------------------------------------------------------------------------------------------
      //non-grouped scan orders
      for (UInt scanTypeIndex = 0; scanTypeIndex < SCAN_NUMBER_OF_TYPES; scanTypeIndex++)
      {
        const COEFF_SCAN_TYPE scanType = COEFF_SCAN_TYPE(scanTypeIndex);
        g_scanOrder[SCAN_UNGROUPED][scanType][log2BlockWidth][log2BlockHeight] = new UInt[totalValues];
        ScanGenerator fullBlockScan(blockWidth, blockHeight, blockWidth, scanType);
        for (UInt scanPosition = 0; scanPosition < totalValues; scanPosition++)
        {
          g_scanOrder[SCAN_UNGROUPED][scanType][log2BlockWidth][log2BlockHeight][scanPosition] = fullBlockScan.GetNextIndex(0, 0);
        }
      }
      //--------------------------------------------------------------------------------------------------
      //grouped scan orders
      const UInt  groupWidth           = 1           << MLS_CG_LOG2_WIDTH;
      const UInt  groupHeight          = 1           << MLS_CG_LOG2_HEIGHT;
      const UInt  widthInGroups        = blockWidth  >> MLS_CG_LOG2_WIDTH;
      const UInt  heightInGroups       = blockHeight >> MLS_CG_LOG2_HEIGHT;
      const UInt  groupSize            = groupWidth    * groupHeight;
      const UInt  totalGroups          = widthInGroups * heightInGroups;
      for (UInt scanTypeIndex = 0; scanTypeIndex < SCAN_NUMBER_OF_TYPES; scanTypeIndex++)
      {
        const COEFF_SCAN_TYPE scanType = COEFF_SCAN_TYPE(scanTypeIndex);
        g_scanOrder[SCAN_GROUPED_4x4][scanType][log2BlockWidth][log2BlockHeight] = new UInt[totalValues];
        ScanGenerator fullBlockScan(widthInGroups, heightInGroups, groupWidth, scanType);
        for (UInt groupIndex = 0; groupIndex < totalGroups; groupIndex++)
        {
          const UInt groupPositionY  = fullBlockScan.GetCurrentY();
          const UInt groupPositionX  = fullBlockScan.GetCurrentX();
          const UInt groupOffsetX    = groupPositionX * groupWidth;
          const UInt groupOffsetY    = groupPositionY * groupHeight;
          const UInt groupOffsetScan = groupIndex     * groupSize;
          ScanGenerator groupScan(groupWidth, groupHeight, blockWidth, scanType);
          for (UInt scanPosition = 0; scanPosition < groupSize; scanPosition++)
          {
            g_scanOrder[SCAN_GROUPED_4x4][scanType][log2BlockWidth][log2BlockHeight][groupOffsetScan + scanPosition] = groupScan.GetNextIndex(groupOffsetX, groupOffsetY);
          }
          fullBlockScan.GetNextIndex(0,0);
        }
      }
      //--------------------------------------------------------------------------------------------------
    }
  }
#if NH_MV
#if NH_MV_HLS_PTL_LIMITS 
 g_generalTierAndLevelLimits[ Level::LEVEL1   ] = TComGeneralTierAndLevelLimits(    36864,     350,  MIN_INT,   16,   1,   1 );
 g_generalTierAndLevelLimits[ Level::LEVEL2   ] = TComGeneralTierAndLevelLimits(   122880,    1500,  MIN_INT,   16,   1,   1 );
 g_generalTierAndLevelLimits[ Level::LEVEL2_1 ] = TComGeneralTierAndLevelLimits(   245760,    3000,  MIN_INT,   20,   1,   1 );
 g_generalTierAndLevelLimits[ Level::LEVEL3   ] = TComGeneralTierAndLevelLimits(   552960,    6000,  MIN_INT,   30,   2,   2 );
 g_generalTierAndLevelLimits[ Level::LEVEL3_1 ] = TComGeneralTierAndLevelLimits(   983040,   10000,  MIN_INT,   40,   3,   3 );
 g_generalTierAndLevelLimits[ Level::LEVEL4   ] = TComGeneralTierAndLevelLimits(  2228224,   12000,    30000,   75,   5,   5 );
 g_generalTierAndLevelLimits[ Level::LEVEL4_1 ] = TComGeneralTierAndLevelLimits(  2228224,   20000,    50000,   75,   5,   5 );
 g_generalTierAndLevelLimits[ Level::LEVEL5   ] = TComGeneralTierAndLevelLimits(  8912896,   25000,   100000,  200,  11,  10 );
 g_generalTierAndLevelLimits[ Level::LEVEL5_1 ] = TComGeneralTierAndLevelLimits(  8912896,   40000,   160000,  200,  11,  10 );
 g_generalTierAndLevelLimits[ Level::LEVEL5_2 ] = TComGeneralTierAndLevelLimits(  8912896,   60000,   240000,  200,  11,  10 );
 g_generalTierAndLevelLimits[ Level::LEVEL6   ] = TComGeneralTierAndLevelLimits( 35651584,   60000,   240000,  600,  22,  20 );
 g_generalTierAndLevelLimits[ Level::LEVEL6_1 ] = TComGeneralTierAndLevelLimits( 35651584,  120000,   480000,  600,  22,  20 );
 g_generalTierAndLevelLimits[ Level::LEVEL6_2 ] = TComGeneralTierAndLevelLimits( 35651584,  240000,   800000,  600,  22,  20 );
#endif
#endif
}
Void destroyROM()
{
  for(UInt groupTypeIndex = 0; groupTypeIndex < SCAN_NUMBER_OF_GROUP_TYPES; groupTypeIndex++)
  {
    for (UInt scanOrderIndex = 0; scanOrderIndex < SCAN_NUMBER_OF_TYPES; scanOrderIndex++)
    {
      for (UInt log2BlockWidth = 0; log2BlockWidth < MAX_CU_DEPTH; log2BlockWidth++)
      {
        for (UInt log2BlockHeight = 0; log2BlockHeight < MAX_CU_DEPTH; log2BlockHeight++)
        {
          delete [] g_scanOrder[groupTypeIndex][scanOrderIndex][log2BlockWidth][log2BlockHeight];
        }
      }
    }
  }
#if NH_3D_DMM
  if( !g_dmmWedgeLists.empty() ) 
  {
    for( UInt ui = 0; ui < g_dmmWedgeLists.size(); ui++ ) { g_dmmWedgeLists[ui].clear(); }
    g_dmmWedgeLists.clear();
  }
  if( !g_dmmWedgeNodeLists.empty() )
  {
    for( UInt ui = 0; ui < g_dmmWedgeNodeLists.size(); ui++ ) { g_dmmWedgeNodeLists[ui].clear(); }
    g_dmmWedgeNodeLists.clear();
  }
#endif
}
// ====================================================================================================================
// Data structure related table & variable
// ====================================================================================================================
UInt g_auiZscanToRaster [ MAX_NUM_PART_IDXS_IN_CTU_WIDTH*MAX_NUM_PART_IDXS_IN_CTU_WIDTH ] = { 0, };
UInt g_auiRasterToZscan [ MAX_NUM_PART_IDXS_IN_CTU_WIDTH*MAX_NUM_PART_IDXS_IN_CTU_WIDTH ] = { 0, };
UInt g_auiRasterToPelX  [ MAX_NUM_PART_IDXS_IN_CTU_WIDTH*MAX_NUM_PART_IDXS_IN_CTU_WIDTH ] = { 0, };
UInt g_auiRasterToPelY  [ MAX_NUM_PART_IDXS_IN_CTU_WIDTH*MAX_NUM_PART_IDXS_IN_CTU_WIDTH ] = { 0, };
const UInt g_auiPUOffset[NUMBER_OF_PART_SIZES] = { 0, 8, 4, 4, 2, 10, 1, 5};
Void initZscanToRaster ( Int iMaxDepth, Int iDepth, UInt uiStartVal, UInt*& rpuiCurrIdx )
{
  Int iStride = 1 << ( iMaxDepth - 1 );
  if ( iDepth == iMaxDepth )
  {
    rpuiCurrIdx[0] = uiStartVal;
    rpuiCurrIdx++;
  }
  else
  {
    Int iStep = iStride >> iDepth;
    initZscanToRaster( iMaxDepth, iDepth+1, uiStartVal,                     rpuiCurrIdx );
    initZscanToRaster( iMaxDepth, iDepth+1, uiStartVal+iStep,               rpuiCurrIdx );
    initZscanToRaster( iMaxDepth, iDepth+1, uiStartVal+iStep*iStride,       rpuiCurrIdx );
    initZscanToRaster( iMaxDepth, iDepth+1, uiStartVal+iStep*iStride+iStep, rpuiCurrIdx );
  }
}
Void initRasterToZscan ( UInt uiMaxCUWidth, UInt uiMaxCUHeight, UInt uiMaxDepth )
{
  UInt  uiMinCUWidth  = uiMaxCUWidth  >> ( uiMaxDepth - 1 );
  UInt  uiMinCUHeight = uiMaxCUHeight >> ( uiMaxDepth - 1 );
  UInt  uiNumPartInWidth  = (UInt)uiMaxCUWidth  / uiMinCUWidth;
  UInt  uiNumPartInHeight = (UInt)uiMaxCUHeight / uiMinCUHeight;
  for ( UInt i = 0; i < uiNumPartInWidth*uiNumPartInHeight; i++ )
  {
    g_auiRasterToZscan[ g_auiZscanToRaster[i] ] = i;
  }
}
Void initRasterToPelXY ( UInt uiMaxCUWidth, UInt uiMaxCUHeight, UInt uiMaxDepth )
{
  UInt    i;
  UInt* uiTempX = &g_auiRasterToPelX[0];
  UInt* uiTempY = &g_auiRasterToPelY[0];
  UInt  uiMinCUWidth  = uiMaxCUWidth  >> ( uiMaxDepth - 1 );
  UInt  uiMinCUHeight = uiMaxCUHeight >> ( uiMaxDepth - 1 );
  UInt  uiNumPartInWidth  = uiMaxCUWidth  / uiMinCUWidth;
  UInt  uiNumPartInHeight = uiMaxCUHeight / uiMinCUHeight;
  uiTempX[0] = 0; uiTempX++;
  for ( i = 1; i < uiNumPartInWidth; i++ )
  {
    uiTempX[0] = uiTempX[-1] + uiMinCUWidth; uiTempX++;
  }
  for ( i = 1; i < uiNumPartInHeight; i++ )
  {
    memcpy(uiTempX, uiTempX-uiNumPartInWidth, sizeof(UInt)*uiNumPartInWidth);
    uiTempX += uiNumPartInWidth;
  }
  for ( i = 1; i < uiNumPartInWidth*uiNumPartInHeight; i++ )
  {
    uiTempY[i] = ( i / uiNumPartInWidth ) * uiMinCUWidth;
  }
}
const Int g_quantScales[SCALING_LIST_REM_NUM] =
{
  26214,23302,20560,18396,16384,14564
};
const Int g_invQuantScales[SCALING_LIST_REM_NUM] =
{
  40,45,51,57,64,72
};
//--------------------------------------------------------------------------------------------------
//structures
#define DEFINE_DST4x4_MATRIX(a,b,c,d) \
{ \
  {  a,  b,  c,  d }, \
  {  c,  c,  0, -c }, \
  {  d, -a, -c,  b }, \
  {  b, -d,  c, -a }, \
}
#define DEFINE_DCT4x4_MATRIX(a,b,c) \
{ \
  { a,  a,  a,  a}, \
  { b,  c, -c, -b}, \
  { a, -a, -a,  a}, \
  { c, -b,  b, -c}  \
}
#define DEFINE_DCT8x8_MATRIX(a,b,c,d,e,f,g) \
{ \
  { a,  a,  a,  a,  a,  a,  a,  a}, \
  { d,  e,  f,  g, -g, -f, -e, -d}, \
  { b,  c, -c, -b, -b, -c,  c,  b}, \
  { e, -g, -d, -f,  f,  d,  g, -e}, \
  { a, -a, -a,  a,  a, -a, -a,  a}, \
  { f, -d,  g,  e, -e, -g,  d, -f}, \
  { c, -b,  b, -c, -c,  b, -b,  c}, \
  { g, -f,  e, -d,  d, -e,  f, -g}  \
}
#define DEFINE_DCT16x16_MATRIX(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o) \
{ \
  { a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a}, \
  { h,  i,  j,  k,  l,  m,  n,  o, -o, -n, -m, -l, -k, -j, -i, -h}, \
  { d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d}, \
  { i,  l,  o, -m, -j, -h, -k, -n,  n,  k,  h,  j,  m, -o, -l, -i}, \
  { b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b}, \
  { j,  o, -k, -i, -n,  l,  h,  m, -m, -h, -l,  n,  i,  k, -o, -j}, \
  { e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e}, \
  { k, -m, -i,  o,  h,  n, -j, -l,  l,  j, -n, -h, -o,  i,  m, -k}, \
  { a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a}, \
  { l, -j, -n,  h, -o, -i,  m,  k, -k, -m,  i,  o, -h,  n,  j, -l}, \
  { f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f}, \
  { m, -h,  l,  n, -i,  k,  o, -j,  j, -o, -k,  i, -n, -l,  h, -m}, \
  { c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c}, \
  { n, -k,  h, -j,  m,  o, -l,  i, -i,  l, -o, -m,  j, -h,  k, -n}, \
  { g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g}, \
  { o, -n,  m, -l,  k, -j,  i, -h,  h, -i,  j, -k,  l, -m,  n, -o}  \
}
#define DEFINE_DCT32x32_MATRIX(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E) \
{ \
  { a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a}, \
  { p,  q,  r,  s,  t,  u,  v,  w,  x,  y,  z,  A,  B,  C,  D,  E, -E, -D, -C, -B, -A, -z, -y, -x, -w, -v, -u, -t, -s, -r, -q, -p}, \
  { h,  i,  j,  k,  l,  m,  n,  o, -o, -n, -m, -l, -k, -j, -i, -h, -h, -i, -j, -k, -l, -m, -n, -o,  o,  n,  m,  l,  k,  j,  i,  h}, \
  { q,  t,  w,  z,  C, -E, -B, -y, -v, -s, -p, -r, -u, -x, -A, -D,  D,  A,  x,  u,  r,  p,  s,  v,  y,  B,  E, -C, -z, -w, -t, -q}, \
  { d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d,  d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d}, \
  { r,  w,  B, -D, -y, -t, -p, -u, -z, -E,  A,  v,  q,  s,  x,  C, -C, -x, -s, -q, -v, -A,  E,  z,  u,  p,  t,  y,  D, -B, -w, -r}, \
  { i,  l,  o, -m, -j, -h, -k, -n,  n,  k,  h,  j,  m, -o, -l, -i, -i, -l, -o,  m,  j,  h,  k,  n, -n, -k, -h, -j, -m,  o,  l,  i}, \
  { s,  z, -D, -w, -p, -v, -C,  A,  t,  r,  y, -E, -x, -q, -u, -B,  B,  u,  q,  x,  E, -y, -r, -t, -A,  C,  v,  p,  w,  D, -z, -s}, \
  { b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b}, \
  { t,  C, -y, -p, -x,  D,  u,  s,  B, -z, -q, -w,  E,  v,  r,  A, -A, -r, -v, -E,  w,  q,  z, -B, -s, -u, -D,  x,  p,  y, -C, -t}, \
  { j,  o, -k, -i, -n,  l,  h,  m, -m, -h, -l,  n,  i,  k, -o, -j, -j, -o,  k,  i,  n, -l, -h, -m,  m,  h,  l, -n, -i, -k,  o,  j}, \
  { u, -E, -t, -v,  D,  s,  w, -C, -r, -x,  B,  q,  y, -A, -p, -z,  z,  p,  A, -y, -q, -B,  x,  r,  C, -w, -s, -D,  v,  t,  E, -u}, \
  { e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e,  e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e}, \
  { v, -B, -p, -C,  u,  w, -A, -q, -D,  t,  x, -z, -r, -E,  s,  y, -y, -s,  E,  r,  z, -x, -t,  D,  q,  A, -w, -u,  C,  p,  B, -v}, \
  { k, -m, -i,  o,  h,  n, -j, -l,  l,  j, -n, -h, -o,  i,  m, -k, -k,  m,  i, -o, -h, -n,  j,  l, -l, -j,  n,  h,  o, -i, -m,  k}, \
  { w, -y, -u,  A,  s, -C, -q,  E,  p,  D, -r, -B,  t,  z, -v, -x,  x,  v, -z, -t,  B,  r, -D, -p, -E,  q,  C, -s, -A,  u,  y, -w}, \
  { a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a}, \
  { x, -v, -z,  t,  B, -r, -D,  p, -E, -q,  C,  s, -A, -u,  y,  w, -w, -y,  u,  A, -s, -C,  q,  E, -p,  D,  r, -B, -t,  z,  v, -x}, \
  { l, -j, -n,  h, -o, -i,  m,  k, -k, -m,  i,  o, -h,  n,  j, -l, -l,  j,  n, -h,  o,  i, -m, -k,  k,  m, -i, -o,  h, -n, -j,  l}, \
  { y, -s, -E,  r, -z, -x,  t,  D, -q,  A,  w, -u, -C,  p, -B, -v,  v,  B, -p,  C,  u, -w, -A,  q, -D, -t,  x,  z, -r,  E,  s, -y}, \
  { f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f,  f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f}, \
  { z, -p,  A,  y, -q,  B,  x, -r,  C,  w, -s,  D,  v, -t,  E,  u, -u, -E,  t, -v, -D,  s, -w, -C,  r, -x, -B,  q, -y, -A,  p, -z}, \
  { m, -h,  l,  n, -i,  k,  o, -j,  j, -o, -k,  i, -n, -l,  h, -m, -m,  h, -l, -n,  i, -k, -o,  j, -j,  o,  k, -i,  n,  l, -h,  m}, \
  { A, -r,  v, -E, -w,  q, -z, -B,  s, -u,  D,  x, -p,  y,  C, -t,  t, -C, -y,  p, -x, -D,  u, -s,  B,  z, -q,  w,  E, -v,  r, -A}, \
  { c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c}, \
  { B, -u,  q, -x,  E,  y, -r,  t, -A, -C,  v, -p,  w, -D, -z,  s, -s,  z,  D, -w,  p, -v,  C,  A, -t,  r, -y, -E,  x, -q,  u, -B}, \
  { n, -k,  h, -j,  m,  o, -l,  i, -i,  l, -o, -m,  j, -h,  k, -n, -n,  k, -h,  j, -m, -o,  l, -i,  i, -l,  o,  m, -j,  h, -k,  n}, \
  { C, -x,  s, -q,  v, -A, -E,  z, -u,  p, -t,  y, -D, -B,  w, -r,  r, -w,  B,  D, -y,  t, -p,  u, -z,  E,  A, -v,  q, -s,  x, -C}, \
  { g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g,  g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g}, \
  { D, -A,  x, -u,  r, -p,  s, -v,  y, -B,  E,  C, -z,  w, -t,  q, -q,  t, -w,  z, -C, -E,  B, -y,  v, -s,  p, -r,  u, -x,  A, -D}, \
  { o, -n,  m, -l,  k, -j,  i, -h,  h, -i,  j, -k,  l, -m,  n, -o, -o,  n, -m,  l, -k,  j, -i,  h, -h,  i, -j,  k, -l,  m, -n,  o}, \
  { E, -D,  C, -B,  A, -z,  y, -x,  w, -v,  u, -t,  s, -r,  q, -p,  p, -q,  r, -s,  t, -u,  v, -w,  x, -y,  z, -A,  B, -C,  D, -E}  \
}
//--------------------------------------------------------------------------------------------------
//coefficients
#if RExt__HIGH_PRECISION_FORWARD_TRANSFORM
const TMatrixCoeff g_aiT4 [TRANSFORM_NUMBER_OF_DIRECTIONS][4][4]   =
{
  DEFINE_DCT4x4_MATRIX  (16384, 21266,  9224),
  DEFINE_DCT4x4_MATRIX  (   64,    83,    36)
};
const TMatrixCoeff g_aiT8 [TRANSFORM_NUMBER_OF_DIRECTIONS][8][8]   =
{
  DEFINE_DCT8x8_MATRIX  (16384, 21266,  9224, 22813, 19244, 12769,  4563),
  DEFINE_DCT8x8_MATRIX  (   64,    83,    36,    89,    75,    50,    18)
};
const TMatrixCoeff g_aiT16[TRANSFORM_NUMBER_OF_DIRECTIONS][16][16] =
{
  DEFINE_DCT16x16_MATRIX(16384, 21266,  9224, 22813, 19244, 12769,  4563, 23120, 22063, 20450, 17972, 14642, 11109,  6446,  2316),
  DEFINE_DCT16x16_MATRIX(   64,    83,    36,    89,    75,    50,    18,    90,    87,    80,    70,    57,    43,    25,     9)
};
const TMatrixCoeff g_aiT32[TRANSFORM_NUMBER_OF_DIRECTIONS][32][32] =
{
  DEFINE_DCT32x32_MATRIX(16384, 21266,  9224, 22813, 19244, 12769,  4563, 23120, 22063, 20450, 17972, 14642, 11109,  6446,  2316, 23106, 22852, 22445, 21848, 20995, 19810, 18601, 17143, 15718, 13853, 11749,  9846,  7908,  5573,  3281,   946),
  DEFINE_DCT32x32_MATRIX(   64,    83,    36,    89,    75,    50,    18,    90,    87,    80,    70,    57,    43,    25,     9,    90,    90,    88,    85,    82,    78,    73,    67,    61,    54,    46,    38,    31,    22,    13,     4)
};
const TMatrixCoeff g_as_DST_MAT_4[TRANSFORM_NUMBER_OF_DIRECTIONS][4][4] =
{
  DEFINE_DST4x4_MATRIX( 7424, 14081, 18893, 21505),
  DEFINE_DST4x4_MATRIX(   29,    55,    74,    84)
};
#else
const TMatrixCoeff g_aiT4 [TRANSFORM_NUMBER_OF_DIRECTIONS][4][4]   =
{
  DEFINE_DCT4x4_MATRIX  (   64,    83,    36),
  DEFINE_DCT4x4_MATRIX  (   64,    83,    36)
};
const TMatrixCoeff g_aiT8 [TRANSFORM_NUMBER_OF_DIRECTIONS][8][8]   =
{
  DEFINE_DCT8x8_MATRIX  (   64,    83,    36,    89,    75,    50,    18),
  DEFINE_DCT8x8_MATRIX  (   64,    83,    36,    89,    75,    50,    18)
};
const TMatrixCoeff g_aiT16[TRANSFORM_NUMBER_OF_DIRECTIONS][16][16] =
{
  DEFINE_DCT16x16_MATRIX(   64,    83,    36,    89,    75,    50,    18,    90,    87,    80,    70,    57,    43,    25,     9),
  DEFINE_DCT16x16_MATRIX(   64,    83,    36,    89,    75,    50,    18,    90,    87,    80,    70,    57,    43,    25,     9)
};
const TMatrixCoeff g_aiT32[TRANSFORM_NUMBER_OF_DIRECTIONS][32][32] =
{
  DEFINE_DCT32x32_MATRIX(   64,    83,    36,    89,    75,    50,    18,    90,    87,    80,    70,    57,    43,    25,     9,    90,    90,    88,    85,    82,    78,    73,    67,    61,    54,    46,    38,    31,    22,    13,     4),
  DEFINE_DCT32x32_MATRIX(   64,    83,    36,    89,    75,    50,    18,    90,    87,    80,    70,    57,    43,    25,     9,    90,    90,    88,    85,    82,    78,    73,    67,    61,    54,    46,    38,    31,    22,    13,     4)
};
const TMatrixCoeff g_as_DST_MAT_4[TRANSFORM_NUMBER_OF_DIRECTIONS][4][4] =
{
  DEFINE_DST4x4_MATRIX(   29,    55,    74,    84),
  DEFINE_DST4x4_MATRIX(   29,    55,    74,    84)
};
#endif
//--------------------------------------------------------------------------------------------------
#undef DEFINE_DST4x4_MATRIX
#undef DEFINE_DCT4x4_MATRIX
#undef DEFINE_DCT8x8_MATRIX
#undef DEFINE_DCT16x16_MATRIX
#undef DEFINE_DCT32x32_MATRIX
//--------------------------------------------------------------------------------------------------
const UChar g_aucChromaScale[NUM_CHROMA_FORMAT][chromaQPMappingTableSize]=
{
  //0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,29,30,31,32,33,33,34,34,35,35,36,36,37,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,51,51,51,51,51,51 },
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,51,51,51,51,51,51 }
};
// ====================================================================================================================
// Intra prediction
// ====================================================================================================================
const UChar g_aucIntraModeNumFast_UseMPM[MAX_CU_DEPTH] =
{
  3,  //   2x2
  8,  //   4x4
  8,  //   8x8
  3,  //  16x16
  3,  //  32x32
  3   //  64x64
};
const UChar g_aucIntraModeNumFast_NotUseMPM[MAX_CU_DEPTH] =
{
  3,  //   2x2
  9,  //   4x4
  9,  //   8x8
  4,  //  16x16   33
  4,  //  32x32   33
  5   //  64x64   33
};
const UChar g_chroma422IntraAngleMappingTable[NUM_INTRA_MODE] =
  //0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, DM
  { 0, 1, 2, 2, 2, 2, 3, 5, 7, 8, 10, 12, 13, 15, 17, 18, 19, 20, 21, 22, 23, 23, 24, 24, 25, 25, 26, 27, 27, 28, 28, 29, 29, 30, 31, DM_CHROMA_IDX};
#if NH_3D_DMM
// ====================================================================================================================
// Depth coding modes
// ====================================================================================================================
const WedgeResolution g_dmmWedgeResolution[6] = 
{
  HALF_PEL,    //   4x4
  HALF_PEL,    //   8x8
  FULL_PEL,    //  16x16
  FULL_PEL,    //  32x32
  FULL_PEL,    //  64x64
  FULL_PEL     // 128x128
};
const UChar g_dmm1TabIdxBits[6] =
{ //2x2   4x4   8x8 16x16 32x32 64x64
     0,    7,   10,   9,    9,   13 };
Bool g_wedgePattern[32*32];
extern std::vector< std::vector<TComWedgelet> >   g_dmmWedgeLists;
extern std::vector< std::vector<TComWedgeNode> >  g_dmmWedgeNodeLists;
#endif
// ====================================================================================================================
// Misc.
// ====================================================================================================================
SChar  g_aucConvertToBit  [ MAX_CU_SIZE+1 ];
#if ENC_DEC_TRACE
FILE*  g_hTrace = NULL; // Set to NULL to open up a file. Set to stdout to use the current output
const Bool g_bEncDecTraceEnable  = true;
const Bool g_bEncDecTraceDisable = false;
Bool   g_HLSTraceEnable = false;
Bool   g_bJustDoIt = false;
UInt64 g_nSymbolCounter = 0;
#if NH_MV_ENC_DEC_TRAC
Bool g_traceCU = false; 
Bool g_tracePU = false; 
Bool g_traceTU = false; 
Bool g_disableNumbering = false; 
Bool g_disableHLSTrace = false; 
UInt64 g_stopAtCounter       = 4660; 
Bool g_traceCopyBack         = false; 
Bool g_decTraceDispDer       = false; 
Bool g_decTraceMvFromMerge   = false; 
Bool g_decTracePicOutput     = false; 
Bool g_startStopTrace          = false; 
Bool g_outputPos             = false;   
Bool g_traceCameraParameters = false; 
Bool g_encNumberOfWrittenBits     = false; 
Bool g_traceEncFracBits        = false; 
Bool g_traceIntraSearchCost    = false; 
Bool g_traceRDCost             = false; 
Bool g_traceSAOCost            = false; 
Bool g_traceModeCheck          = false; 
UInt g_indent                  = false; 
Bool g_decNumBitsRead          = false; 
Bool g_traceMotionInfoBeforUniPred = false; 
Bool g_traceMergeCandListConst = false; 
Bool g_traceBitsRead          = false; 
Bool g_traceSubPBMotion       = false; 
#endif
#endif
// ====================================================================================================================
// Scanning order & context model mapping
// ====================================================================================================================
// scanning order table
UInt* g_scanOrder[SCAN_NUMBER_OF_GROUP_TYPES][SCAN_NUMBER_OF_TYPES][ MAX_CU_DEPTH ][ MAX_CU_DEPTH ];
const UInt ctxIndMap4x4[4*4] =
{
  0, 1, 4, 5,
  2, 3, 4, 5,
  6, 6, 8, 8,
  7, 7, 8, 8
};
const UInt g_uiMinInGroup[ LAST_SIGNIFICANT_GROUPS ] = {0,1,2,3,4,6,8,12,16,24};
const UInt g_uiGroupIdx[ MAX_TU_SIZE ]   = {0,1,2,3,4,4,5,5,6,6,6,6,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9};
const TChar *MatrixType[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM] =
{
  {
    "INTRA4X4_LUMA",
    "INTRA4X4_CHROMAU",
    "INTRA4X4_CHROMAV",
    "INTER4X4_LUMA",
    "INTER4X4_CHROMAU",
    "INTER4X4_CHROMAV"
  },
  {
    "INTRA8X8_LUMA",
    "INTRA8X8_CHROMAU",
    "INTRA8X8_CHROMAV",
    "INTER8X8_LUMA",
    "INTER8X8_CHROMAU",
    "INTER8X8_CHROMAV"
  },
  {
    "INTRA16X16_LUMA",
    "INTRA16X16_CHROMAU",
    "INTRA16X16_CHROMAV",
    "INTER16X16_LUMA",
    "INTER16X16_CHROMAU",
    "INTER16X16_CHROMAV"
  },
  {
   "INTRA32X32_LUMA",
   "INTRA32X32_CHROMAU_FROM16x16_CHROMAU",
   "INTRA32X32_CHROMAV_FROM16x16_CHROMAV",
   "INTER32X32_LUMA",
   "INTER32X32_CHROMAU_FROM16x16_CHROMAU",
   "INTER32X32_CHROMAV_FROM16x16_CHROMAV"
  },
};
const TChar *MatrixType_DC[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM] =
{
  {
  },
  {
  },
  {
    "INTRA16X16_LUMA_DC",
    "INTRA16X16_CHROMAU_DC",
    "INTRA16X16_CHROMAV_DC",
    "INTER16X16_LUMA_DC",
    "INTER16X16_CHROMAU_DC",
    "INTER16X16_CHROMAV_DC"
  },
  {
    "INTRA32X32_LUMA_DC",
    "INTRA32X32_CHROMAU_DC_FROM16x16_CHROMAU",
    "INTRA32X32_CHROMAV_DC_FROM16x16_CHROMAV",
    "INTER32X32_LUMA_DC",
    "INTER32X32_CHROMAU_DC_FROM16x16_CHROMAU",
    "INTER32X32_CHROMAV_DC_FROM16x16_CHROMAV"
  },
};
const Int g_quantTSDefault4x4[4*4] =
{
  16,16,16,16,
  16,16,16,16,
  16,16,16,16,
  16,16,16,16
};
const Int g_quantIntraDefault8x8[8*8] =
{
  16,16,16,16,17,18,21,24,
  16,16,16,16,17,19,22,25,
  16,16,17,18,20,22,25,29,
  16,16,18,21,24,27,31,36,
  17,17,20,24,30,35,41,47,
  18,19,22,27,35,44,54,65,
  21,22,25,31,41,54,70,88,
  24,25,29,36,47,65,88,115
};
const Int g_quantInterDefault8x8[8*8] =
{
  16,16,16,16,17,18,20,24,
  16,16,16,17,18,20,24,25,
  16,16,17,18,20,24,25,28,
  16,17,18,20,24,25,28,33,
  17,18,20,24,25,28,33,41,
  18,20,24,25,28,33,41,54,
  20,24,25,28,33,41,54,71,
  24,25,28,33,41,54,71,91
};
const UInt g_scalingListSize   [SCALING_LIST_SIZE_NUM] = {16,64,256,1024};
const UInt g_scalingListSizeX  [SCALING_LIST_SIZE_NUM] = { 4, 8, 16,  32};
#if NH_MV_ENC_DEC_TRAC
#if ENC_DEC_TRACE
Void tracePSHeader( const TChar* psName, Int layerId )
{  
  if ( !g_disableHLSTrace  ) 
  {
    fprintf( g_hTrace, "=========== ");    
    fprintf( g_hTrace, "%s", psName );    
    fprintf( g_hTrace, " Layer %d ===========", layerId );    
    fprintf( g_hTrace, "\n" );    
    fflush ( g_hTrace );    
  }
}
Void stopAtPos( Int poc, Int layerId, Int cuPelX, Int cuPelY, Int cuWidth, Int cuHeight )
{
  if ( g_outputPos ) 
  {
    std::cout << "POC\t"        << poc 
              << "\tLayerId\t"  << layerId 
              << "\tCuPelX\t"   << cuPelX  
              << "\tCuPelY\t"   << cuPelY 
              << "\tCuWidth\t"  << cuWidth
              << "\tCuHeight\t" << cuHeight
              << std::endl; 
  }
  Bool startTrace = false; 
  if ( g_startStopTrace && poc == 0 && layerId == 0 )
  {
    startTrace = ( cuPelX  == 0 ) && ( cuPelY  == 0 ) && ( cuWidth == 64 ) && ( cuHeight == 64 ); 
    }
  if ( startTrace )
  { 
    g_outputPos              = true; 
    g_traceEncFracBits       = false;   
    g_traceIntraSearchCost   = false;     
    g_encNumberOfWrittenBits = false;     
    g_traceRDCost            = true; 
    g_traceModeCheck         = true; 
    g_traceCopyBack          = false; 
    }
  Bool stopTrace = false; 
  if ( g_startStopTrace && poc == 0 && layerId == 0 )
  {
    stopTrace = ( cuPelX  == 128 ) && ( cuPelY  == 0 ) && ( cuWidth == 64 ) && ( cuHeight == 64 ); 
  }
  if ( stopTrace )
  { 
    g_outputPos              = false; 
    g_traceModeCheck         = false; 
    g_traceEncFracBits       = false; 
    g_traceIntraSearchCost   = false;        
    g_encNumberOfWrittenBits = false; 
    g_traceRDCost            = false; 
    g_traceCopyBack          = false;     
  }  
}
Void writeToTraceFile( const TChar* symbolName, Int val, Bool doIt )
{
  if ( ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) && doIt  ) 
  {
    incSymbolCounter();  
    if ( !g_disableNumbering )
    {  
      fprintf( g_hTrace, "%8lld  ", g_nSymbolCounter );
    }
    fprintf( g_hTrace, "%-50s       : %d\n", symbolName, val );     
    fflush ( g_hTrace );   
  }
}
UInt64 incSymbolCounter( )
{
  g_nSymbolCounter++;  
  if ( g_stopAtCounter == g_nSymbolCounter )
  {
    std::cout << "Break point here." << std::endl; 
  }  
  return g_nSymbolCounter; 
}
Void writeToTraceFile( const TChar* symbolName, Bool doIt )
{
  if ( ( ( g_nSymbolCounter >= COUNTER_START && g_nSymbolCounter <= COUNTER_END )|| g_bJustDoIt ) && doIt  ) 
  {
    incSymbolCounter(); 
    fprintf( g_hTrace, "%s", symbolName );    
    fflush ( g_hTrace );    
  }
}
Void printStr( std::string str )
{
  std::cout << str << std::endl; 
}
Void printStrIndent( Bool b, std::string strStr )
{
  if ( b )
  {  
    std::cout << std::string(g_indent, ' ');
    printStr( strStr );
  }
}
Void prinStrIncIndent( Bool b,  std::string strStr )
{
  if ( b )
  {
    printStrIndent( true,  strStr );
    if (g_indent < 50)
    {
      g_indent++;
    }
  }  
}
Void decIndent( Bool b )
{
  if (b && g_indent > 0)
  {
    g_indent--;  
  }  
}
#endif
#endif
#if NH_3D_DMM
std::vector< std::vector<TComWedgelet>  > g_dmmWedgeLists;
std::vector< std::vector<TComWedgeNode> > g_dmmWedgeNodeLists;
Void initWedgeLists( Bool initNodeList )
{
  if( !g_dmmWedgeLists.empty() ) return;
  for( UInt ui = g_aucConvertToBit[DMM_MIN_SIZE]; ui < (g_aucConvertToBit[DMM_MAX_SIZE]); ui++ )
  {
    UInt uiWedgeBlockSize = ((UInt)DMM_MIN_SIZE)<<ui;
    std::vector<TComWedgelet> acWedgeList;
    std::vector<TComWedgeRef> acWedgeRefList;
    createWedgeList( uiWedgeBlockSize, uiWedgeBlockSize, acWedgeList, acWedgeRefList, g_dmmWedgeResolution[ui] );
    g_dmmWedgeLists.push_back( acWedgeList );
    if( initNodeList )
    {
      // create WedgeNodeList
      std::vector<TComWedgeNode> acWedgeNodeList;
      for( UInt uiPos = 0; uiPos < acWedgeList.size(); uiPos++ )
      {
        if( acWedgeList[uiPos].getIsCoarse() )
        {
          TComWedgeNode cWedgeNode;
          cWedgeNode.setPatternIdx( uiPos );
          UInt uiRefPos = 0;
          for( Int iOffS = -1; iOffS <= 1; iOffS++ )
          {
            for( Int iOffE = -1; iOffE <= 1; iOffE++ )
            {
              if( iOffS == 0 && iOffE == 0 ) { continue; }
              Int iSx = (Int)acWedgeList[uiPos].getStartX();
              Int iSy = (Int)acWedgeList[uiPos].getStartY();
              Int iEx = (Int)acWedgeList[uiPos].getEndX();
              Int iEy = (Int)acWedgeList[uiPos].getEndY();
              switch( acWedgeList[uiPos].getOri() )
              {
              case( 0 ): { iSx += iOffS; iEy += iOffE; } break;
              case( 1 ): { iSy += iOffS; iEx -= iOffE; } break;
              case( 2 ): { iSx -= iOffS; iEy -= iOffE; } break;
              case( 3 ): { iSy -= iOffS; iEx += iOffE; } break;
              case( 4 ): { iSx += iOffS; iEx += iOffE; } break;
              case( 5 ): { iSy += iOffS; iEy += iOffE; } break;
              default: assert( 0 );
              }
              for( UInt k = 0; k < acWedgeRefList.size(); k++ )
              {
                if( iSx == (Int)acWedgeRefList[k].getStartX() && 
                  iSy == (Int)acWedgeRefList[k].getStartY() && 
                  iEx == (Int)acWedgeRefList[k].getEndX()   && 
                  iEy == (Int)acWedgeRefList[k].getEndY()      )
                {
                  if( acWedgeRefList[k].getRefIdx() != cWedgeNode.getPatternIdx() )
                  {
                    Bool bNew = true;
                    for( UInt m = 0; m < uiRefPos; m++ ) { if( acWedgeRefList[k].getRefIdx() == cWedgeNode.getRefineIdx( m ) ) { bNew = false; break; } }
                    if( bNew ) 
                    {
                      cWedgeNode.setRefineIdx( acWedgeRefList[k].getRefIdx(), uiRefPos );
                      uiRefPos++;
                      break;
                    }
                  }
                }
              }
            }
          }
          acWedgeNodeList.push_back( cWedgeNode );
        }
      }
      g_dmmWedgeNodeLists.push_back( acWedgeNodeList );
    }
  }
}
Void createWedgeList( UInt uiWidth, UInt uiHeight, std::vector<TComWedgelet> &racWedgeList, std::vector<TComWedgeRef> &racWedgeRefList, WedgeResolution eWedgeRes )
{
  assert( uiWidth == uiHeight );
  Int posStart = 0, posEnd = 0;
  UInt uiBlockSize = 0;
  switch( eWedgeRes )
  {
  case(   FULL_PEL ): { uiBlockSize =  uiWidth;     break; }
  case(   HALF_PEL ): { uiBlockSize = (uiWidth<<1); break; }
  }
  TComWedgelet cTempWedgelet( uiWidth, uiHeight );
  for( UInt uiOri = 0; uiOri < 6; uiOri++ )
  {
    posEnd = (Int) racWedgeList.size();
    if (uiOri == 0 || uiOri == 4)
    {
    for( Int iK = 0; iK < uiBlockSize; iK += (uiWidth>=16 ?2:1))
    {
      for( Int iL = 0; iL < uiBlockSize; iL += ((uiWidth>=16 && uiOri<4)?2:1) )
      {
        Int xS = iK;
        Int yS = 0;
        Int xE = (uiOri == 0) ? 0 : iL;
        Int yE = (uiOri == 0) ? iL : uiBlockSize - 1;
        cTempWedgelet.setWedgelet( xS, yS, xE, yE, uiOri, eWedgeRes, ((iL%2)==0 && (iK%2)==0) );
        addWedgeletToList( cTempWedgelet, racWedgeList, racWedgeRefList );
      }
    }
    }
    else
    {
      for (Int pos = posStart; pos < posEnd; pos++)
      {
        cTempWedgelet.generateWedgePatternByRotate(racWedgeList[pos], uiOri);
        addWedgeletToList( cTempWedgelet, racWedgeList, racWedgeRefList );
      }
    }
    posStart = posEnd;
  }
}
Void addWedgeletToList( TComWedgelet cWedgelet, std::vector<TComWedgelet> &racWedgeList, std::vector<TComWedgeRef> &racWedgeRefList )
{
  Bool bValid = cWedgelet.checkNotPlain();
  if( bValid )
  {
    for( UInt uiPos = 0; uiPos < racWedgeList.size(); uiPos++ )
    {
      if( cWedgelet.checkIdentical( racWedgeList[uiPos].getPattern() ) )
      {
        TComWedgeRef cWedgeRef;
        cWedgeRef.setWedgeRef( cWedgelet.getStartX(), cWedgelet.getStartY(), cWedgelet.getEndX(), cWedgelet.getEndY(), uiPos );
        racWedgeRefList.push_back( cWedgeRef );
        bValid = false;
        return;
      }
    }
  }
  if( bValid )
  {
    for( UInt uiPos = 0; uiPos < racWedgeList.size(); uiPos++ )
    {
      if( cWedgelet.checkInvIdentical( racWedgeList[uiPos].getPattern() ) )
      {
        TComWedgeRef cWedgeRef;
        cWedgeRef.setWedgeRef( cWedgelet.getStartX(), cWedgelet.getStartY(), cWedgelet.getEndX(), cWedgelet.getEndY(), uiPos );
        racWedgeRefList.push_back( cWedgeRef );
        bValid = false;
        return;
      }
    }
  }
  if( bValid )
  {
    racWedgeList.push_back( cWedgelet );
    TComWedgeRef cWedgeRef;
    cWedgeRef.setWedgeRef( cWedgelet.getStartX(), cWedgelet.getStartY(), cWedgelet.getEndX(), cWedgelet.getEndY(), (UInt)(racWedgeList.size()-1) );
    racWedgeRefList.push_back( cWedgeRef );
  }
}
WedgeList* getWedgeListScaled( UInt blkSize ) 
{ 
  return &g_dmmWedgeLists[ g_aucConvertToBit[( 16 >= blkSize ) ? blkSize : 16] ]; 
}
WedgeNodeList* getWedgeNodeListScaled( UInt blkSize )
{
  return &g_dmmWedgeNodeLists[ g_aucConvertToBit[( 16 >= blkSize ) ? blkSize : 16] ]; 
}
#endif //NH_3D_DMM
//! \}
