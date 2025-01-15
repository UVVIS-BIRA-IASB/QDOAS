/*
Qdoas is a cross-platform application for spectral analysis with the DOAS
algorithm.  Copyright (C) 2007  S[&]T and BIRA

*/


#ifndef _CENGINERESPONSE_H_GUARD
#define _CENGINERESPONSE_H_GUARD

#include <vector>
#include <string>

#include "CPlotDataSet.h"
#include "CPlotImage.h"
#include "CTablePageData.h"
#include "CEngineError.h"
#include "mediate_types.h"

class CEngineController;

//------------------------------------------------------------

class CEngineResponse
{
 public:
  CEngineResponse() : m_highestErrorLevel(0) {};
  virtual ~CEngineResponse() {};

  void addErrorMessage(const std::string &tag, const std::string &msg, int errorLevel);

  virtual void process(CEngineController *engineController) = 0;

  bool processErrors(CEngineController *engineController);

  bool hasErrors(void) const;
  bool hasFatalError(void) const;

 protected:
  int m_highestErrorLevel;
  std::vector<CEngineError> m_errorMessages;
};

inline bool CEngineResponse::hasErrors(void) const { return !m_errorMessages.empty(); }
inline bool CEngineResponse::hasFatalError(void) const { return (m_highestErrorLevel == FatalEngineError); }

//------------------------------------------------------------

class CEngineResponseMessage : public CEngineResponse
{
 public:
  virtual void process(CEngineController *engineController);
};

//------------------------------------------------------------

class CEngineResponseVisual : public CEngineResponse
{
 public:
  virtual ~CEngineResponseVisual();

  virtual void process(CEngineController *engineController);

  void addDataSet(int pageNumber, const CPlotDataSet *dataSet);
  void addImage(int pageNumber,const CPlotImage *plotImage);
  void addPageTitleAndTag(int pageNumber, const std::string &title, const std::string &tag);
  void addCell(int pageNumber, int row, int col, const cell_data &data);

 protected:
  std::vector<SPlotData> m_plotDataList;
  std::vector<STitleTag> m_titleList;
  std::vector<SCell> m_cellList;
  std::vector<SPlotImage> m_plotImageList;
};

//------------------------------------------------------------

class CEngineResponseBeginAccessFile : public CEngineResponseVisual
{
 public:
  CEngineResponseBeginAccessFile(const std::string& fileName) : m_fileName(fileName),
                                                                m_numberOfRecords(-1) {};

  virtual void process(CEngineController *engineController);

  void setNumberOfRecords(int numberOfRecords);

 private:
  std::string m_fileName;
  int m_numberOfRecords;
};

//------------------------------------------------------------

class CEngineResponseSpecificRecord : public CEngineResponseVisual
{
 public:
  CEngineResponseSpecificRecord() : m_recordNumber(-1) {};

  virtual void process(CEngineController *engineController);

  void setRecordNumber(int recordNumber);

 private:
  int m_recordNumber;
};

#endif

