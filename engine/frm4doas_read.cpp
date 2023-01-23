
//  ----------------------------------------------------------------------------
//! \addtogroup Format
//! @{
//!
//! \file      frm4doas_read.cpp
//! \brief     Routines to read data in the netCDF format used in the FRM<sub>4</sub>DOAS network.
//! \details   FRM<sub>4</sub>DOAS holds for Fiducial Reference Measurements for Ground-Based
//!            DOAS Air-Quality Observations (ESA Contract No. 4000118181/16/I-EF).  The purpose
//!            of this project is (2016-2017) to develop a centralised system providing harmonised
//!            ground-based reference data from a network of MAXDOAS instruments within a
//!            short latency period.
//! \details   For further information, see http://frm4doas.aeronomie.be/index.php
//! \authors   Caroline FAYT (qdoas@aeronomie.be)
//! \date      18/12/2017 (creation date)
//! \bug       Known issue (20/12/2017) with Windows : netCDF functions fail for compressed variables (zlib=True) when used with hdf5.dll compiled from source.  \n
//!            Work around : using the hdf5.dll of the precompiled library (4.5.0) seems to solve the problem.
//! \todo      Load reference spectrum + slit function      \n
//!            Check if the wavelength calibration exists   \n
//!            Complete with general ground-based fields
//! \copyright QDOAS is distributed under GNU General Public License
//!
//! @}
//  ----------------------------------------------------------------------------
//
//  FUNCTIONS
//
//  FRM4DOAS_Read_Metadata  load variables from the metadata group + scan index from the measurements group
//  FRM4DOAS_Set            open the netCDF file, get the number of records and load metadata variables
//  FRM4DOAS_Read           read a specified record from a file in the netCDF format implemented for the FRM4DOAS project
//  FRM4DOAS_Cleanup        close the current file and release allocated buffers
//
//  ----------------------------------------------------------------------------
//
//  QDOAS is a cross-platform application developed in QT for DOAS retrieval
//  (Differential Optical Absorption Spectroscopy).
//
//        BIRA-IASB
//        Belgian Institute for Space Aeronomy
//        Ringlaan 3 Avenue Circulaire
//        1180     UCCLE
//        BELGIUM
//        qdoas@aeronomie.be
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or (at
//  your option) any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  ----------------------------------------------------------------------------

// =======
// INCLUDE
// =======

#include <string>
#include <vector>
#include <array>
#include <set>
#include <tuple>
#include <stdexcept>
#include <algorithm>
#include <sstream>

#include <cassert>
#include <cmath>

#include "frm4doas_read.h"
#include "netcdfwrapper.h"
// #include "dir_iter.h"

extern "C" {
#include "winthrd.h"
#include "comdefs.h"
#include "stdfunc.h"
#include "engine_context.h"
#include "engine.h"
#include "mediate.h"
#include "analyse.h"
#include "spline.h"
#include "vector.h"
#include "zenithal.h"
}

using std::string;
using std::vector;
using std::set;

// ======================
// STRUCTURES DEFINITIONS
// ======================

  // irradiance reference ---> !!!! CHECK AND COMPLETE

  struct refspec {
    refspec() : lambda(), irradiance(), sigma() {};
    vector<double> lambda;
    vector<double> irradiance;
    vector<double> sigma;
  };

//! \struct frm4doas_data_fields
//! \brief This structure defined in frm4doas_read.cpp module mainly contains variables present in the metadata group of the netCDF file (format used in the FRM<sub>4</sub>DOAS network).
//! \details Vectors are automatically allocated when the file is open and released when the file is closed.  It is only locally used by functions of the module frm4doas_read.cpp.
//! \details Five groups are present in netCDF files :\n
//! \details \li <strong>ancillary</strong> for ancillary data;\n
//!          \li <strong>meteorological_data</strong> (temperature and pressures profiles)\n
//!          \li <strong>instrument_location</strong> (name of the station, latitude, longitude and altitude of the instrument)\n
//!          \li <strong>keydata</strong> (reference spectrum, measured slit function(s))\n
//!          \li <strong>measurements</strong> : radiances, instrumental errors if known, scan index and wavelength calibration\n
//!          \li <strong>frm4doas_data_fields</strong> : currently limited to data present in ASCII files proposed for CINDI-2 reprocessing but could be extended to any data relevant for ground-based measurements (for example, temperature of the detector, �)\n
//! \details <strong>frm4doas_data_fields</strong> group contains information useful to QDOAS to describe the records.

enum _frm4doas_data_fields
 {
  FRM4DOAS_FIELD_LAT,                                                           //!< \details Latitude
  FRM4DOAS_FIELD_LON,                                                           //!< \details Longitude
  FRM4DOAS_FIELD_ALT,                                                           //!< \details Altitude

  FRM4DOAS_FIELD_VAA,                                                           //!< \details Viewing azimuth angle
  FRM4DOAS_FIELD_VEA,                                                           //!< \details Viewing elevation angle
  FRM4DOAS_FIELD_SZA,                                                           //!< \details Solar zenith angle
  FRM4DOAS_FIELD_SAA,                                                           //!< \details Solar azimuth angle
  FRM4DOAS_FIELD_MEA,                                                           //!< \details Moon elevation angle
  FRM4DOAS_FIELD_MAA,                                                           //!< \details Moon azimuth angle

  FRM4DOAS_FIELD_TINT,                                                          //!< \details Exposure time
  FRM4DOAS_FIELD_TAT,                                                           //!< \details Total acquisition time
  FRM4DOAS_FIELD_TMT,                                                           //!< \details Total measurement time
  FRM4DOAS_FIELD_NACC,                                                          //!< \details Number of co-added spectra
  FRM4DOAS_FIELD_MT,                                                            //!< \details Measurement type
  FRM4DOAS_FIELD_DT,                                                            //!< \details Datetime (date and time at half of the measurement (UT YYYY,MM,DD,hh,mm,ss,ms)
  FRM4DOAS_FIELD_DTS,                                                           //!< \details Datetime_start (date and time at the beginning of the measurement (UT YYYY,MM,DD,hh,mm,ss,ms)
  FRM4DOAS_FIELD_DTE,                                                           //!< \details Datetime_end (date and time at the end of the measurement (UT YYYY,MM,DD,hh,mm,ss,ms)
  FRM4DOAS_FIELD_SCI,                                                           //!< \details Scan index
  FRM4DOAS_FIELD_ZBI,                                                           //!< \details Zenith before index
  FRM4DOAS_FIELD_ZAI,                                                           //!< \details Zenith after index
  FRM4DOAS_FIELD_MAX
 };

static struct netcdf_data_fields frm4doas_data_fields[FRM4DOAS_FIELD_MAX]=
 {
   { "/INSTRUMENT_LOCATION", "latitude", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                             // FRM4DOAS_FIELD_LAT
   { "/INSTRUMENT_LOCATION", "longitude", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                            // FRM4DOAS_FIELD_LON
   { "/INSTRUMENT_LOCATION", "altitude", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                             // FRM4DOAS_FIELD_ALT
                                                                                                                //
   { "/RADIANCE/GEODATA", "viewing_azimuth_angle", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                   // FRM4DOAS_FIELD_VAA
   { "/RADIANCE/GEODATA", "viewing_elevation_angle", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                 // FRM4DOAS_FIELD_VEA
   { "/RADIANCE/GEODATA", "solar_zenith_angle", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                      // FRM4DOAS_FIELD_SZA
   { "/RADIANCE/GEODATA", "solar_azimuth_angle", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                     // FRM4DOAS_FIELD_SAA
   { "/RADIANCE/GEODATA", "moon_elevation_angle", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                    // FRM4DOAS_FIELD_MEA
   { "/RADIANCE/GEODATA", "moon_azimuth_angle", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                      // FRM4DOAS_FIELD_MAA
                                                                                                                //
   { "/RADIANCE/OBSERVATIONS", "exposure_time", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },                      // FRM4DOAS_FIELD_TINT
   { "/RADIANCE/OBSERVATIONS", "total_acquisition_time", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },             // FRM4DOAS_FIELD_TAT
   { "/RADIANCE/OBSERVATIONS", "total_measurement_time", { 0, 0, 0, 0, 0, 0 }, 0, NC_FLOAT, NULL },             // FRM4DOAS_FIELD_TMT
   { "/RADIANCE/OBSERVATIONS", "number_of_coadded_spectra", { 0, 0, 0, 0, 0, 0 }, 0, NC_INT, NULL },            // FRM4DOAS_FIELD_NACC
   { "/RADIANCE/OBSERVATIONS", "measurement_type", { 0, 0, 0, 0, 0, 0 }, 0, NC_INT, NULL },                     // FRM4DOAS_FIELD_MT
   { "/RADIANCE/OBSERVATIONS", "datetime", { 0, 0, 0, 0, 0, 0 }, 0, NC_SHORT, NULL },                           // FRM4DOAS_FIELD_DT
   { "/RADIANCE/OBSERVATIONS", "datetime_start", { 0, 0, 0, 0, 0, 0 }, 0, NC_SHORT, NULL },                     // FRM4DOAS_FIELD_DTS
   { "/RADIANCE/OBSERVATIONS", "datetime_end", { 0, 0, 0, 0, 0, 0 }, 0, NC_SHORT, NULL },                       // FRM4DOAS_FIELD_DTE
   { "/RADIANCE/OBSERVATIONS", "scan_index", { 0, 0, 0, 0, 0, 0 }, 0, NC_SHORT, NULL },                         // FRM4DOAS_FIELD_SCI
   { "/RADIANCE/OBSERVATIONS", "index_zenith_before", { 0, 0, 0, 0, 0, 0 }, 0, NC_SHORT, NULL },                // FRM4DOAS_FIELD_ZBI
   { "/RADIANCE/OBSERVATIONS", "index_zenith_after", { 0, 0, 0, 0, 0, 0 }, 0, NC_SHORT, NULL }                  // FRM4DOAS_FIELD_ZAI
 };

// ================
// STATIC VARIABLES
// ================

static NetCDFFile current_file;                                                  //!< \details Pointer to the current netCDF file
static string root_name;                                                         //!< \details The name of the root (should be the basename of the file)
static size_t det_size;                                                          //!< \details The current detector size

// -----------------------------------------------------------------------------
// FUNCTION FRM4DOAS_Set
// -----------------------------------------------------------------------------
//!
//! \fn      RC FRM4DOAS_Set(ENGINE_CONTEXT *pEngineContext)
//! \details Open the netCDF file, get the number of records and load metadata variables.\n
//! \param   [in]  pEngineContext  pointer to the engine context;  some fields are affected by this function.\n
//! \return  ERROR_ID_NETCDF on run time error (opening of the file didn't succeed, missing variable...)\n
//!          ERROR_ID_NO on success
//!
// -----------------------------------------------------------------------------

RC FRM4DOAS_Set(ENGINE_CONTEXT *pEngineContext)
 {
  // Declarations

  RC rc = ERROR_ID_NO;
  ENGINE_refStartDate=1;

  // Try to open the file and load metadata

  try
   {
    current_file = NetCDFFile(pEngineContext->fileInfo.fileName,NC_NOWRITE);     // open file
    root_name = current_file.getName();

    NetCDFGroup root_group = current_file.getGroup(root_name);                   // go to the root

    pEngineContext->n_alongtrack=
    pEngineContext->recordNumber=root_group.dimLen("number_of_records");                   // get the record number

    pEngineContext->n_crosstrack=1;                                              // spectra should be one dimension only
    det_size=root_group.dimLen("detector_size");                                      // get the number of pixels of the detector

    for (int i=0; i<(int)det_size; i++)
     pEngineContext->buffers.irrad[i]=(double)0.;

    // Read metadata

    current_file.read_data_fields(frm4doas_data_fields,FRM4DOAS_FIELD_MAX);

    short *dt=(short *)frm4doas_data_fields[FRM4DOAS_FIELD_DTS].varData;

    pEngineContext->fileInfo.startDate.da_day=(char)dt[2];
    pEngineContext->fileInfo.startDate.da_mon=(char)dt[1];
    pEngineContext->fileInfo.startDate.da_year=dt[0];

    THRD_localShift=((float *)frm4doas_data_fields[FRM4DOAS_FIELD_LON].varData)[0]/15.;
   }
  catch (std::runtime_error& e)
   {
    rc = ERROR_SetLast(__func__, ERROR_TYPE_FATAL, ERROR_ID_NETCDF, e.what());   // in case of error, capture the message
   }

  // Return

  return rc;
}

// -----------------------------------------------------------------------------
// FUNCTION FRM4DOAS_Read
// -----------------------------------------------------------------------------
//!
//! /fn      RC FRM4DOAS_Read(ENGINE_CONTEXT *pEngineContext,int recordNo,int dateFlag,int localDay)
//! /details Read a specified record from a file in the netCDF format implemented for the FRM<sub>4</sub>DOAS project\n
//! /param   [in]  pEngineContext  pointer to the engine context;  some fields are affected by this function.\n
//! /param   [in]  recordNo        the index of the record to read\n
//! /param   [in]  dateFlag        1 to search for a reference spectrum;  0 otherwise\n
//! /param   [in]  localDay        if /a dateFlag is 1, the calendar day for the reference spectrum to search for\n
//! /return  ERROR_ID_FILE_END if the requested record number is not found\n
//!          ERROR_ID_FILE_RECORD if the requested record doesn't satisfy current criteria (for example for the selection of the reference)\n
//!          ERROR_ID_NO on success
//!
// -----------------------------------------------------------------------------

RC FRM4DOAS_Read(ENGINE_CONTEXT *pEngineContext,int recordNo,int dateFlag,int localDay)
 {
  // Declarations

  NetCDFGroup measurements_group;                                                // measurement group in the netCDF file
  RECORD_INFO *pRecordInfo;                                                      // pointer to the record structure in the engine context
  double tmLocal;                                                                // calculation of the local time (in number of seconds)
  vector<float> wve;                                                             // wavelength calibration
  vector<float> spe;                                                             // spectrum
  vector<float> err;                                                             // instrumental errors
  vector<short> qf;                                                              // quality flag
  int measurementType;
  const size_t i_alongtrack=(recordNo-1); // /col_dim;
  const size_t i_crosstrack=(recordNo-1); // %col_dim;                                                                    // index for loops and arrays
  RC rc;                                                                         // return code

  // Initializations

  pRecordInfo=&pEngineContext->recordInfo;
  rc = ERROR_ID_NO;

  // The requested record is out of range

  if ((recordNo<=0) || (recordNo>pEngineContext->recordNumber))
   rc=ERROR_ID_FILE_END;
  else
   {
    // Goto the requested record

    const size_t start[] = {(size_t)(recordNo-1), 0};
    const size_t count[] = {(size_t)1, det_size};                                // only one record to load

    // Spectra

    if (!dateFlag)
     {
      measurements_group=current_file.getGroup(root_name+"/RADIANCE/OBSERVATIONS");

      measurements_group.getVar("wavelength",start,count,2,(float)0.,wve);
      measurements_group.getVar("radiance",start,count,2,(float)0.,spe);
      measurements_group.getVar("radiance_error",start,count,2,(float)1.,err);
      measurements_group.getVar("radiance_quality_flag",start,count,2,(short)1,qf);

      for (int i=0; i<det_size; i++)
       {
        pEngineContext->buffers.lambda_irrad[i]=(double)wve[i];     // Check and complete
        pEngineContext->buffers.lambda[i]=wve[i];

        pEngineContext->buffers.spectrum[i]=spe[i];
        pEngineContext->buffers.sigmaSpec[i]=err[i];
       }
     }

    // Date and time fields (UT YYYY,MM,DD,hh,mm,ss,ms)

    short *dt=(short *)frm4doas_data_fields[FRM4DOAS_FIELD_DT].varData;
    short *dts=(short *)frm4doas_data_fields[FRM4DOAS_FIELD_DTS].varData;
    short *dte=(short *)frm4doas_data_fields[FRM4DOAS_FIELD_DTE].varData;

    pRecordInfo->present_datetime.thedate.da_day=(char)dt[i_alongtrack*7+2];
    pRecordInfo->present_datetime.thedate.da_mon=(char)dt[i_alongtrack*7+1];
    pRecordInfo->present_datetime.thedate.da_year=dt[i_alongtrack*7+0];
    pRecordInfo->present_datetime.thetime.ti_hour=(char)dt[i_alongtrack*7+3];
    pRecordInfo->present_datetime.thetime.ti_min=(char)dt[i_alongtrack*7+4];
    pRecordInfo->present_datetime.thetime.ti_sec=(char)dt[i_alongtrack*7+5];
    pRecordInfo->present_datetime.millis=dt[i_alongtrack*7+6];

    pRecordInfo->startDateTime.thedate.da_day=(char)dts[i_alongtrack*7+2];
    pRecordInfo->startDateTime.thedate.da_mon=(char)dts[i_alongtrack*7+1];
    pRecordInfo->startDateTime.thedate.da_year=dts[i_alongtrack*7+0];
    pRecordInfo->startDateTime.thetime.ti_hour=(char)dts[i_alongtrack*7+3];
    pRecordInfo->startDateTime.thetime.ti_min=(char)dts[i_alongtrack*7+4];
    pRecordInfo->startDateTime.thetime.ti_sec=(char)dts[i_alongtrack*7+5];
    pRecordInfo->startDateTime.millis=dts[i_alongtrack*7+6];

    pRecordInfo->endDateTime.thedate.da_day=(char)dte[i_alongtrack*7+2];
    pRecordInfo->endDateTime.thedate.da_mon=(char)dte[i_alongtrack*7+1];
    pRecordInfo->endDateTime.thedate.da_year=dte[i_alongtrack*7+0];
    pRecordInfo->endDateTime.thetime.ti_hour=(char)dte[i_alongtrack*7+3];
    pRecordInfo->endDateTime.thetime.ti_min=(char)dte[i_alongtrack*7+4];
    pRecordInfo->endDateTime.thetime.ti_sec=(char)dte[i_alongtrack*7+5];
    pRecordInfo->endDateTime.millis=dte[i_alongtrack*7+6];

    // Other metadata

    size_t ilat=(frm4doas_data_fields[FRM4DOAS_FIELD_LAT].varData!=NULL)?frm4doas_data_fields[FRM4DOAS_FIELD_LAT].varDimsLen[0]:0;

    pRecordInfo->longitude=(frm4doas_data_fields[FRM4DOAS_FIELD_LON].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_LON].varData)[(ilat==pEngineContext->n_alongtrack)?i_alongtrack:0]:0.;
    pRecordInfo->latitude=(frm4doas_data_fields[FRM4DOAS_FIELD_LAT].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_LAT].varData)[(ilat==pEngineContext->n_alongtrack)?i_alongtrack:0]:0.;
    pRecordInfo->altitude=(frm4doas_data_fields[FRM4DOAS_FIELD_ALT].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_ALT].varData)[(ilat==pEngineContext->n_alongtrack)?i_alongtrack:0]:0.;

    pRecordInfo->azimuthViewAngle=(frm4doas_data_fields[FRM4DOAS_FIELD_VAA].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_VAA].varData)[i_alongtrack] : 0.;
    pRecordInfo->elevationViewAngle=(frm4doas_data_fields[FRM4DOAS_FIELD_VEA].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_VEA].varData)[i_alongtrack] : 0.;
    pRecordInfo->Zm=(frm4doas_data_fields[FRM4DOAS_FIELD_SZA].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_SZA].varData)[i_alongtrack] : 0.;
    pRecordInfo->Azimuth=(frm4doas_data_fields[FRM4DOAS_FIELD_SAA].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_SAA].varData)[i_alongtrack] : 0.;

    pRecordInfo->Tint=(frm4doas_data_fields[FRM4DOAS_FIELD_TINT].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_TINT].varData)[i_alongtrack] : 0.;
    pRecordInfo->TotalAcqTime=(frm4doas_data_fields[FRM4DOAS_FIELD_TAT].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_TAT].varData)[i_alongtrack] : 0.;
    pRecordInfo->TotalExpTime=(frm4doas_data_fields[FRM4DOAS_FIELD_TMT].varData!=NULL)?(float)((float *)frm4doas_data_fields[FRM4DOAS_FIELD_TMT].varData)[i_alongtrack] : 0.;

    pRecordInfo->NSomme=(frm4doas_data_fields[FRM4DOAS_FIELD_NACC].varData!=NULL)?(int)((int *)frm4doas_data_fields[FRM4DOAS_FIELD_NACC].varData)[i_alongtrack] : 0;
    pRecordInfo->maxdoas.measurementType=(frm4doas_data_fields[FRM4DOAS_FIELD_MT].varData!=NULL)?(int)((int *)frm4doas_data_fields[FRM4DOAS_FIELD_MT].varData)[i_alongtrack] : 0;

    pRecordInfo->maxdoas.scanIndex=(frm4doas_data_fields[FRM4DOAS_FIELD_SCI].varData!=NULL)?(short)((short *)frm4doas_data_fields[FRM4DOAS_FIELD_SCI].varData)[i_alongtrack] : ITEM_NONE;
    pRecordInfo->maxdoas.zenithBeforeIndex=(frm4doas_data_fields[FRM4DOAS_FIELD_ZBI].varData!=NULL)?(short)((short *)frm4doas_data_fields[FRM4DOAS_FIELD_ZBI].varData)[i_alongtrack] : ITEM_NONE;
    pRecordInfo->maxdoas.zenithAfterIndex=(frm4doas_data_fields[FRM4DOAS_FIELD_ZAI].varData!=NULL)?(short)((short *)frm4doas_data_fields[FRM4DOAS_FIELD_ZAI].varData)[i_alongtrack] : ITEM_NONE;

    if (pRecordInfo->NSomme<0)   // -1 means that the information is not available
     pRecordInfo->NSomme=1;

    pRecordInfo->Tm=(double)ZEN_NbSec(&pRecordInfo->present_datetime.thedate,&pRecordInfo->present_datetime.thetime,0);

    tmLocal=pRecordInfo->Tm+THRD_localShift*3600.;
    pRecordInfo->localCalDay=ZEN_FNCaljda(&tmLocal);

    // Recalculate solar zenith angle if necessary

    if (std::isnan(pRecordInfo->Zm) && !std::isnan(pRecordInfo->longitude) && !std::isnan(pRecordInfo->latitude))
     {
      double longitude;
      longitude=-pRecordInfo->longitude;
      pRecordInfo->Zm=ZEN_FNTdiz(ZEN_FNCrtjul(&pRecordInfo->Tm),&longitude,&pRecordInfo->latitude,&pRecordInfo->Azimuth);

      pRecordInfo->Azimuth+=180.;  // because the used convention is 0..360, 0° Northward and ZEN_FNTdiz is -180..180
     }

    // Selection of the reference spectrum

    measurementType=pEngineContext->project.instrumental.user;

    if (rc || (dateFlag && ((pRecordInfo->maxdoas.measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_ZENITH) ||
             ((fabs(pRecordInfo->elevationViewAngle+1.)>EPSILON) &&
             ((pRecordInfo->elevationViewAngle<pEngineContext->project.spectra.refAngle-pEngineContext->project.spectra.refTol) ||
              (pRecordInfo->elevationViewAngle>pEngineContext->project.spectra.refAngle+pEngineContext->project.spectra.refTol))))))
     rc=ERROR_ID_FILE_RECORD;

    else if (!dateFlag && (measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_NONE))
     {
      if (((measurementType==PRJCT_INSTR_MAXDOAS_TYPE_OFFAXIS) && (pRecordInfo->maxdoas.measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_OFFAXIS) && (pRecordInfo->maxdoas.measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_ZENITH)) ||
          ((measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_OFFAXIS) && (pRecordInfo->maxdoas.measurementType!=measurementType)))

       rc=ERROR_ID_FILE_RECORD;
     }

    // Later : add the selection of the measurement type in the instrumental page else if (!dateFlag && (measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_NONE))
    // Later : add the selection of the measurement type in the instrumental page  {
    // Later : add the selection of the measurement type in the instrumental page      if (((measurementType==PRJCT_INSTR_MAXDOAS_TYPE_OFFAXIS) && (pRecordInfo->maxdoas.measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_OFFAXIS) && (pRecordInfo->maxdoas.measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_ZENITH)) ||
    // Later : add the selection of the measurement type in the instrumental page          ((measurementType!=PRJCT_INSTR_MAXDOAS_TYPE_OFFAXIS) && (pRecordInfo->maxdoas.measurementType!=measurementType)))
    // Later : add the selection of the measurement type in the instrumental page
    // Later : add the selection of the measurement type in the instrumental page       rc=ERROR_ID_FILE_RECORD;
    // Later : add the selection of the measurement type in the instrumental page  }
   }

  // Return

  return rc;
 }

// -----------------------------------------------------------------------------
// FUNCTION FRM4DOAS_Cleanup
// -----------------------------------------------------------------------------
//!
//! /fn      void FRM4DOAS_Cleanup(void)
//! /details Close the current file and release allocated buffers\n
//!
// -----------------------------------------------------------------------------

void FRM4DOAS_Cleanup(void)
 {
  current_file.release_data_fields();
  current_file.close();
 }

