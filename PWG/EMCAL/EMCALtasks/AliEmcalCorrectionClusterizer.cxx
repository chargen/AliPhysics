// AliEmcalCorrectionClusterizer
//
/**************************************************************************
 * Copyright(c) 1998-1999, ALICE Experiment at CERN, All rights reserved. *
 *                                                                        *
 * Author: The ALICE Off-line Project.                                    *
 * Contributors are mentioned in the code where appropriate.              *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/

// --- Root ---
#include <TGeoManager.h>
#include <TObjArray.h>
#include <TString.h>
#include <TTree.h>
#include <TArrayI.h>

// --- AliRoot ---
#include "AliCDBEntry.h"
#include "AliCDBManager.h"
#include "AliCaloCalibPedestal.h"
#include "AliEMCALAfterBurnerUF.h"
#include "AliEMCALCalibData.h"
#include "AliEMCALClusterizerNxN.h"
#include "AliEMCALClusterizerv1.h"
#include "AliEMCALClusterizerv2.h"
#include "AliEMCALClusterizerFixedWindow.h"
#include "AliEMCALDigit.h"
#include "AliEMCALGeometry.h"
#include "AliEMCALRecParam.h"
#include "AliEMCALRecPoint.h"
#include "AliInputEventHandler.h"

#include "AliEmcalCorrectionClusterizer.h"

/// \cond CLASSIMP
ClassImp(AliEmcalCorrectionClusterizer);
/// \endcond

// Actually registers the class with the base class
RegisterCorrectionComponent<AliEmcalCorrectionClusterizer> AliEmcalCorrectionClusterizer::reg("AliEmcalCorrectionClusterizer");

//________________________________________________________________________
AliEmcalCorrectionClusterizer::AliEmcalCorrectionClusterizer() :
  AliEmcalCorrectionComponent("AliEmcalCorrectionClusterizer"),
  fDigitsArr(0),
  fClusterArr(0),
  fRecParam(new AliEMCALRecParam),
  fClusterizer(0),
  fUnfolder(0),
  fJustUnfold(kFALSE),
  fGeomName(),
  fGeomMatrixSet(kFALSE),
  fLoadGeomMatrices(kFALSE),
  fOCDBpath(),
  fCalibData(0),
  fPedestalData(0),
  fLoadCalib(kFALSE),
  fLoadPed(kFALSE),
  fSubBackground(kFALSE),
  fNPhi(4),
  fNEta(4),
  fShiftPhi(2),
  fShiftEta(2),
  fTRUShift(0),
  fInputCellType(kFEEData),
  fSetCellMCLabelFromCluster(0),
  fSetCellMCLabelFromEdepFrac(0),
  fRemapMCLabelForAODs(0),
  fCaloClusters(0),
  fEsd(0),
  fAod(0),
  fRecalDistToBadChannels(kFALSE),
  fRecalShowerShape(kFALSE)
{
  // Default constructor
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));
  
  for(Int_t i = 0; i < AliEMCALGeoParams::fgkEMCALModules; i++) fGeomMatrix[i] = 0 ;
  for(Int_t j = 0; j < fgkTotalCellNumber;                 j++)
  { fOrgClusterCellId[j] =-1; fCellLabels[j] =-1 ; }
}

//________________________________________________________________________
AliEmcalCorrectionClusterizer::~AliEmcalCorrectionClusterizer()
{
  // Destructor
  
  delete fClusterizer;
  delete fUnfolder;
  delete fRecParam;
}

//________________________________________________________________________
Bool_t AliEmcalCorrectionClusterizer::Initialize()
{
  // Initialization
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));
  AliEmcalCorrectionComponent::Initialize();
  // Do base class initializations and if it fails -> bail out
  //AliAnalysisTaskEmcal::ExecOnce();
  //if (!fInitialized) return;
  
  std::string clusterizerTypeStr = "";
  GetProperty("clusterizer", clusterizerTypeStr);
  UInt_t clusterizerType = clusterizerTypeMap.at(clusterizerTypeStr);
  Double_t cellE  = 0.05;
  GetProperty("cellE", cellE);
  Double_t seedE  = 0.1;
  GetProperty("seedE", seedE);
  Float_t timeMin = -1;             //minimum time of physical signal in a cell/digit (s) (in run macro, -50e-6)
  GetProperty("cellTimeMin", timeMin);
  Float_t timeMax = +1;             //maximum time of physical signal in a cell/digit (s) (in run macro, 50e-6)
  GetProperty("cellTimeMax", timeMax);
  Float_t timeCut =  1;             //maximum time difference between the digits inside EMC cluster (s) (in run macro, 1e-6)
  GetProperty("clusterTimeLength", timeCut);
  Float_t w0 = 4.5;
  GetProperty("w0", w0);
  GetProperty("recalDistToBadChannels", fRecalDistToBadChannels);
  GetProperty("recalShowerShape", fRecalShowerShape);
  GetProperty("remapMcAod", fRemapMCLabelForAODs);
  Bool_t enableFracEMCRecalc = kFALSE;
  GetProperty("enableFracEMCRecalc", enableFracEMCRecalc);
  Float_t diffEAggregation = 0.;
  GetProperty("diffEAggregation", diffEAggregation);

  AddContainer(kCluster);
  
  fRecParam->SetClusterizerFlag(clusterizerType);
  fRecParam->SetMinECut(cellE);
  fRecParam->SetClusteringThreshold(seedE);
  fRecParam->SetW0(w0);
  fRecParam->SetTimeMin(timeMin);
  fRecParam->SetTimeMax(timeMax);
  fRecParam->SetTimeCut(timeCut);
  fRecParam->SetLocMaxCut(diffEAggregation);      // Set minimum energy difference to start new cluster
  
  if (clusterizerType == AliEMCALRecParam::kClusterizerNxN)
    fRecParam->SetNxM(1,1); // -> (1,1) means 3x3!
  
  if(enableFracEMCRecalc){
    fSetCellMCLabelFromEdepFrac = kTRUE;
    fSetCellMCLabelFromCluster  = 0;
  }
  
  fInputCellType = AliEmcalCorrectionClusterizer::kFEEData;
  Printf("inputCellType: %d",fInputCellType);
  
  return kTRUE;
}

//________________________________________________________________________
Bool_t AliEmcalCorrectionClusterizer::Run()
{
  // Run
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));
  AliEmcalCorrectionComponent::Run();
  
  // Main loop, called for each event
  
  fEsd = dynamic_cast<AliESDEvent*>(fEvent);
  fAod = dynamic_cast<AliAODEvent*>(fEvent);
  
  fCaloClusters = fClusCont->GetArray();
  
  UInt_t offtrigger = 0;
  if (fEsd) {
    UInt_t mask1 = fEsd->GetESDRun()->GetDetectorsInDAQ();
    UInt_t mask2 = fEsd->GetESDRun()->GetDetectorsInReco();
    Bool_t desc1 = (mask1 >> 18) & 0x1;
    Bool_t desc2 = (mask2 >> 18) & 0x1;
    if (desc1==0 || desc2==0) { //AliDAQ::OfflineModuleName(180=="EMCAL"
      AliError(Form("EMCAL not in DAQ/RECO: %u (%u)/%u (%u)",
                    mask1, fEsd->GetESDRun()->GetDetectorsInReco(),
                    mask2, fEsd->GetESDRun()->GetDetectorsInDAQ()));
      return kFALSE;
    }
    AliAnalysisManager *am = AliAnalysisManager::GetAnalysisManager();
    offtrigger = ((AliInputEventHandler*)(am->GetInputEventHandler()))->IsEventSelected();
  } else {
    offtrigger =  ((AliVAODHeader*)fAod->GetHeader())->GetOfflineTrigger();
  }
  
  if (!fMCEvent) {
    if (offtrigger & AliVEvent::kFastOnly) {
      AliError(Form("EMCAL not in fast only partition"));
      return kFALSE;
    }
  }
  
  Init();
  
  if (fJustUnfold) {
    AliWarning("Unfolding not implemented");
    return kTRUE;
  }
  
  FillDigitsArray();
  
  Clusterize();
  
  UpdateClusters();
  
  CalibrateClusters();
  
  return kTRUE;
}

//________________________________________________________________________
void AliEmcalCorrectionClusterizer::Clusterize()
{
  // Clusterize
  
  if (fSubBackground) {
    fClusterizer->SetInputCalibrated(kTRUE);
    fClusterizer->SetCalibrationParameters(0);
  }
  
  fClusterizer->Digits2Clusters("");
  
  if (fSubBackground) {
    if (fCalibData) {
      fClusterizer->SetInputCalibrated(kFALSE);
      fClusterizer->SetCalibrationParameters(fCalibData);
    }
  }
}

//________________________________________________________________________
void AliEmcalCorrectionClusterizer::FillDigitsArray()
{
  // Fill digits array
  
  fDigitsArr->Clear("C");
  switch (fInputCellType) {
      
    case kFEEData :
    case kFEEDataMCOnly :
    case kFEEDataExcludeMC :
    {
      // In case of MC productions done before aliroot tag v5-02-Rev09
      // passing the cluster label to all the cells belonging to this cluster
      // very rough
      // Copied and simplified from AliEMCALTenderSupply
      if (fSetCellMCLabelFromCluster || fSetCellMCLabelFromEdepFrac)
      {
        for (Int_t i = 0; i < fgkTotalCellNumber; i++)
        {
          fCellLabels      [i] =-1 ;
          fOrgClusterCellId[i] =-1 ;
        }
        
        Int_t nClusters = fEvent->GetNumberOfCaloClusters();
        for (Int_t i = 0; i < nClusters; i++)
        {
          AliVCluster *clus =  fEvent->GetCaloCluster(i);
          
          if (!clus) continue;
          
          if (!clus->IsEMCAL()) continue ;
          
          Int_t      label = clus->GetLabel();
          UShort_t * index = clus->GetCellsAbsId() ;
          
          for(Int_t icell=0; icell < clus->GetNCells(); icell++)
          {
            if(!fSetCellMCLabelFromEdepFrac)
              fCellLabels[index[icell]] = label;
            
            fOrgClusterCellId[index[icell]] = i ; // index of the original cluster
          } // cell in cluster loop
        } // cluster loop
      }
      
      Double_t avgE        = 0; // for background subtraction
      const Int_t ncells   = fCaloCells->GetNumberOfCells();
      for (Int_t icell = 0, idigit = 0; icell < ncells; ++icell)
      {
        Double_t cellAmplitude=0, cellTime=0, cellEFrac = 0;
        Short_t  cellNumber=0;
        Int_t cellMCLabel=-1;
        if (fCaloCells->GetCell(icell, cellNumber, cellAmplitude, cellTime, cellMCLabel, cellEFrac) != kTRUE)
          break;
        
        //if (fSetCellMCLabelFromCluster) cellMCLabel = fCellLabels[cellNumber];
        if(!fSetCellMCLabelFromEdepFrac)
        {
          if      (fSetCellMCLabelFromCluster) cellMCLabel = fCellLabels[cellNumber];
          else if (fRemapMCLabelForAODs      ) RemapMCLabelForAODs(cellMCLabel);
        }
        
        if (cellMCLabel > 0 && cellEFrac < 1e-6)
          cellEFrac = 1;
        
        if (cellAmplitude < 1e-6 || cellNumber < 0)
          continue;
        
        if (fInputCellType == kFEEDataMCOnly) {
          if (cellMCLabel <= 0)
            continue;
          else {
            cellAmplitude *= cellEFrac;
            cellEFrac = 1;
          }
        }
        else if (fInputCellType == kFEEDataExcludeMC) {
          if (cellMCLabel > 0)
            continue;
          else {
            cellAmplitude *= 1 - cellEFrac;
            cellEFrac = 0;
          }
        }
        
        AliEMCALDigit *digit = new((*fDigitsArr)[idigit]) AliEMCALDigit(cellMCLabel, cellMCLabel, cellNumber,
                                                                        (Float_t)cellAmplitude, (Float_t)cellTime,
                                                                        AliEMCALDigit::kHG,idigit, 0, 0, cellEFrac*cellAmplitude);
        
        if (fSubBackground)
        {
          Float_t energy = cellAmplitude;
          Float_t time   = cellTime;
          fClusterizer->Calibrate(energy,time,cellNumber);
          digit->SetAmplitude(energy);
          avgE += energy;
        }
        
        // New way to set the cell MC labels,
        // valid only for MC productions with aliroot > v5-07-21
        if(fSetCellMCLabelFromEdepFrac && fOrgClusterCellId[cellNumber] >= 0) // index can be negative if noisy cell that did not form cluster
        {
          fCellLabels[cellNumber] = idigit;
          
          AliVCluster *clus = 0;
          Int_t iclus = fOrgClusterCellId[cellNumber];
          
          if(iclus < 0)
          {
            AliInfo("Negative original cluster index, skip \n");
            continue;
          }
          
          clus = fEvent->GetCaloCluster(iclus);
          
          for(Int_t icluscell=0; icluscell < clus->GetNCells(); icluscell++ )
          {
            if(cellNumber != clus->GetCellAbsId(icluscell)) continue ;
            
            // Get the energy deposition fraction.
            Float_t eDepFrac[4];
            clus->GetCellMCEdepFractionArray(icluscell,eDepFrac);
            
            // Select the MC label contributing, only if enough energy deposition
            TArrayI labeArr(0);
            TArrayF eDepArr(0);
            Int_t nLabels = 0;
            for(Int_t imc = 0; imc < 4; imc++)
            {
              if(eDepFrac[imc] > 0 && clus->GetNLabels() > imc)
              {
                nLabels++;
                
                labeArr.Set(nLabels);
                labeArr.AddAt(clus->GetLabelAt(imc), nLabels-1);
                
                eDepArr.Set(nLabels);
                eDepArr.AddAt(eDepFrac[imc]*cellAmplitude, nLabels-1);
                // use as deposited energy a fraction of the simulated energy (smeared and with noise)
              }
            }
            
            if(nLabels > 0)
            {
              digit->SetListOfParents(nLabels,labeArr.GetArray(),eDepArr.GetArray());
            }
          }
        }
        
        
        idigit++;
      }
      
      if (fSubBackground) {
        avgE /= fGeom->GetNumberOfSuperModules()*48*24;
        Int_t ndigis = fDigitsArr->GetEntries();
        for (Int_t i = 0; i < ndigis; ++i) {
          AliEMCALDigit *digit = static_cast<AliEMCALDigit*>(fDigitsArr->At(i));
          Double_t energy = digit->GetAmplitude() - avgE;
          if (energy<=0.001) {
            digit->SetAmplitude(0);
          } else {
            digit->SetAmplitude(energy);
          }
        }
      }
    }
      break;
      
    case kPattern :
    {
      // Fill digits from a pattern
      Int_t maxd = fGeom->GetNCells() / 4;
      for (Int_t idigit = 0; idigit < maxd; idigit++){
        if (idigit % 24 == 12) idigit += 12;
        AliEMCALDigit *digit = static_cast<AliEMCALDigit*>(fDigitsArr->New(idigit));
        digit->SetId(idigit * 4);
        digit->SetTime(600);
        digit->SetTimeR(600);
        digit->SetIndexInList(idigit);
        digit->SetType(AliEMCALDigit::kHG);
        digit->SetAmplitude(0.1);
      }
    }
      break;
      
    case kL0FastORs    :
    case kL0FastORsTC  :
    case kL1FastORs    :
    {
      // Fill digits from FastORs
      
      AliVCaloTrigger *triggers = fEvent->GetCaloTrigger("EMCAL");
      
      if (!triggers || !(triggers->GetEntries() > 0))
        return;
      
      Int_t idigit = 0;
      triggers->Reset();
      
      while ((triggers->Next())) {
        Float_t L0Amplitude = 0;
        triggers->GetAmplitude(L0Amplitude);
        
        if (L0Amplitude <= 0 && fInputCellType != kL1FastORs)
          continue;
        
        Int_t L1Amplitude = 0;
        triggers->GetL1TimeSum(L1Amplitude);
        
        if (L1Amplitude <= 0 && fInputCellType == kL1FastORs)
          continue;
        
        Int_t triggerTime = 0;
        Int_t ntimes = 0;
        triggers->GetNL0Times(ntimes);
        
        if (ntimes < 1 && fInputCellType == kL0FastORsTC)
          continue;
        
        if (ntimes > 0) {
          Int_t trgtimes[25];
          triggers->GetL0Times(trgtimes);
          triggerTime = trgtimes[0];
        }
        
        Int_t triggerCol = 0, triggerRow = 0;
        triggers->GetPosition(triggerCol, triggerRow);
        
        Int_t find = -1;
        fGeom->GetAbsFastORIndexFromPositionInEMCAL(triggerCol, triggerRow, find);
        
        if (find < 0)
          continue;
        
        Int_t cidx[4] = {-1};
        Bool_t ret = fGeom->GetCellIndexFromFastORIndex(find, cidx);
        
        if (!ret)
          continue;
        
        Float_t triggerAmplitude = 0;
        
        if (fInputCellType == kL1FastORs) {
          triggerAmplitude = 0.25 * L1Amplitude;  // it will add 4 cells for 1 amplitude
        }
        else {
          triggerAmplitude = L0Amplitude;      // 10 bit truncated, so it is already divided by 4
        }
        
        for (Int_t idxpos = 0; idxpos < 4; idxpos++) {
          Int_t triggerNumber = cidx[idxpos];
          AliEMCALDigit *digit = static_cast<AliEMCALDigit*>(fDigitsArr->New(idigit));
          digit->SetId(triggerNumber);
          digit->SetTime(triggerTime);
          digit->SetTimeR(triggerTime);
          digit->SetIndexInList(idigit);
          digit->SetType(AliEMCALDigit::kHG);
          digit->SetAmplitude(triggerAmplitude);
          idigit++;
        }
      }
    }
      break;
  }
}

//________________________________________________________________________________________
void AliEmcalCorrectionClusterizer::RecPoints2Clusters(TClonesArray *clus)
{
  // Convert AliEMCALRecoPoints to AliESDCaloClusters/AliAODCaloClusters.
  // Cluster energy, global position, cells and their amplitude fractions are restored.
  
  const Int_t Ncls = fClusterArr->GetEntries();
  AliDebug(1, Form("total no of clusters %d", Ncls));
  
  for(Int_t i=0, nout=clus->GetEntries(); i < Ncls; ++i)
  {
    AliEMCALRecPoint *recpoint = static_cast<AliEMCALRecPoint*>(fClusterArr->At(i));
    
    Int_t ncellsTrue = 0;
    const Int_t ncells = recpoint->GetMultiplicity();
    UShort_t   absIds[ncells];
    Double32_t ratios[ncells];
    Int_t   *dlist = recpoint->GetDigitsList();
    Float_t *elist = recpoint->GetEnergiesList();
    Double_t mcEnergy = 0;
    
    for (Int_t c = 0; c < ncells; ++c)
    {
      AliEMCALDigit *digit = static_cast<AliEMCALDigit*>(fDigitsArr->At(dlist[c]));
      absIds[ncellsTrue] = digit->GetId();
      ratios[ncellsTrue] = elist[c]/digit->GetAmplitude();
      
      if (digit->GetIparent(1) > 0)
        mcEnergy += digit->GetDEParent(1)/recpoint->GetEnergy();
      
      ++ncellsTrue;
    }
    
    if (ncellsTrue < 1)
    {
      AliWarning("Skipping cluster with no cells");
      continue;
    }
    
    // calculate new cluster position
    TVector3 gpos;
    recpoint->GetGlobalPosition(gpos);
    Float_t g[3];
    gpos.GetXYZ(g);
    
    AliDebug(1, Form("energy %f", recpoint->GetEnergy()));
    
    AliVCluster *c = static_cast<AliVCluster*>(clus->New(nout++));
    c->SetType(AliVCluster::kEMCALClusterv1);
    c->SetE(recpoint->GetEnergy());
    c->SetPosition(g);
    c->SetNCells(ncellsTrue);
    c->SetCellsAbsId(absIds);
    c->SetCellsAmplitudeFraction(ratios);
    c->SetID(recpoint->GetUniqueID());
    c->SetDispersion(recpoint->GetDispersion());
    c->SetEmcCpvDistance(-1);
    c->SetChi2(-1);
    c->SetTOF(recpoint->GetTime()) ;     //time-of-flight
    c->SetNExMax(recpoint->GetNExMax()); //number of local maxima
    Float_t elipAxis[2];
    recpoint->GetElipsAxis(elipAxis);
    c->SetM02(elipAxis[0]*elipAxis[0]);
    c->SetM20(elipAxis[1]*elipAxis[1]);
    c->SetMCEnergyFraction(mcEnergy);
    
    //
    // MC labels
    //
    Int_t    parentMult   = 0;
    Int_t   *parentList   = recpoint->GetParents(parentMult);
    Float_t *parentListDE = recpoint->GetParentsDE();  // deposited energy
    
    c->SetLabel(parentList, parentMult);
    c->SetClusterMCEdepFractionFromEdepArray(parentListDE);
    
    //
    // Set the cell energy deposition fraction map:
    //
    if( parentMult > 0 && fSetCellMCLabelFromEdepFrac )
    {
      UInt_t * mcEdepFracPerCell = new UInt_t[ncellsTrue];
      
      // Get the digit that originated this cell cluster
      //AliVCaloCells* cells = InputEvent()->GetEMCALCells();
      
      for(Int_t icell = 0; icell < ncellsTrue ; icell++)
      {
        Int_t   idigit  = fCellLabels[absIds[icell]];
        
        const AliEMCALDigit * dig = (const AliEMCALDigit*)fDigitsArr->At(idigit);
        
        // Find the 4 MC labels that contributed to the cluster and their
        // deposited energy in the current digit
        
        mcEdepFracPerCell[icell] = 0; // init
        
        Int_t  nparents   = dig->GetNiparent();
        if ( nparents > 0 )
        {
          Int_t   digLabel   =-1 ;
          Float_t edep       = 0 ;
          Float_t edepTot    = 0 ;
          Float_t mcEDepFrac[4] = {0,0,0,0};
          
          // all parents in digit
          for ( Int_t jndex = 0 ; jndex < nparents ; jndex++ )
          {
            digLabel = dig->GetIparent (jndex+1);
            edep     = dig->GetDEParent(jndex+1);
            edepTot += edep;
            
            if       ( digLabel == parentList[0] ) mcEDepFrac[0] = edep;
            else  if ( digLabel == parentList[1] ) mcEDepFrac[1] = edep;
            else  if ( digLabel == parentList[2] ) mcEDepFrac[2] = edep;
            else  if ( digLabel == parentList[3] ) mcEDepFrac[3] = edep;
          } // all prarents in digit
          
          // Divide energy deposit by total deposited energy
          // Do this only when deposited energy is significant, use 10 MeV although 50 MeV should be expected
          if(edepTot > 0.01)
          {
            mcEdepFracPerCell[icell] = c->PackMCEdepFraction(mcEDepFrac);
          }
        } // at least one parent label in digit
      } // cell in cluster loop
      
      c->SetCellsMCEdepFractionMap(mcEdepFracPerCell);
      
      delete [] mcEdepFracPerCell;
      
    } // at least one parent in cluster, do the cell primary packing
  }
}

//________________________________________________________________________
void AliEmcalCorrectionClusterizer::UpdateClusters()
{
  // Update cells in case re-calibration was done.
  
  // Before destroying the orignal list, assign to the rec points the MC labels
  // of the original clusters, if requested
  if (fSetCellMCLabelFromCluster == 2)
    SetClustersMCLabelFromOriginalClusters() ;
  
  const Int_t nents = fCaloClusters->GetEntries();
  for (Int_t i=0;i<nents;++i) {
    AliVCluster *c = static_cast<AliVCluster*>(fCaloClusters->At(i));
    if (!c)
      continue;
    if (c->IsEMCAL())
      delete fCaloClusters->RemoveAt(i);
  }
  
  fCaloClusters->Compress();
  
  RecPoints2Clusters(fCaloClusters);
}

//________________________________________________________________________________________
void AliEmcalCorrectionClusterizer::CalibrateClusters()
{
  // Go through clusters one by one and process separate correction
  
  Int_t nclusters = fCaloClusters->GetEntriesFast();
  for (Int_t icluster=0; icluster < nclusters; ++icluster) {
    AliVCluster *clust = static_cast<AliVCluster*>(fCaloClusters->At(icluster));
    if (!clust)
      continue;
    
    // SHOWER SHAPE -----------------------------------------------
    if (fRecalShowerShape)
      fRecoUtils->RecalculateClusterShowerShapeParameters(fGeom, fCaloCells, clust);
    
    // DISTANCE TO BAD CHANNELS -----------------------------------
    if (fRecalDistToBadChannels)
      fRecoUtils->RecalculateClusterDistanceToBadChannel(fGeom, fCaloCells, clust);
  }
  
  fCaloClusters->Compress();
}

//___________________________________________________________
void AliEmcalCorrectionClusterizer::RemapMCLabelForAODs(Int_t & label)
{
  // MC label for Cells not remapped after ESD filtering, do it here.
  
  if (label < 0) return;
  
  TClonesArray * arr = dynamic_cast<TClonesArray*>(fAod->FindListObject("mcparticles")) ;
  if (!arr) return ;
  
  if (label < arr->GetEntriesFast())
  {
    AliAODMCParticle * particle = dynamic_cast<AliAODMCParticle *>(arr->At(label));
    if (!particle) return ;
    
    if (label == particle->Label()) return ; // label already OK
  }
  
  // loop on the particles list and check if there is one with the same label
  for (Int_t ind = 0; ind < arr->GetEntriesFast(); ind++)
  {
    AliAODMCParticle * particle = dynamic_cast<AliAODMCParticle *>(arr->At(ind));
    if (!particle) continue ;
    
    if (label == particle->Label())
    {
      label = ind;
      return;
    }
  }
  
  label = -1;
}

//_____________________________________________________________________________________________
void AliEmcalCorrectionClusterizer::SetClustersMCLabelFromOriginalClusters()
{
  // Get the original clusters that contribute to the new rec point cluster,
  // assign the labels of such clusters to the new rec point cluster.
  // Only approximatedly valid  when output are V1 clusters, or input/output clusterizer
  // are the same handle with care
  // Copy from same method in AliAnalysisTaskEMCALClusterize, but here modify the recpoint and
  // not the output calocluster
  
  Int_t ncls = fClusterArr->GetEntriesFast();
  for(Int_t irp=0; irp < ncls; ++irp)
  {
    TArrayI clArray(300) ; //Weird if more than a few clusters are in the origin ...
    clArray.Reset();
    Int_t nClu = 0;
    Int_t nLabTotOrg = 0;
    Float_t emax = -1;
    Int_t idMax = -1;
    
    AliEMCALRecPoint *clus = static_cast<AliEMCALRecPoint*>(fClusterArr->At(irp));
    
    //Find the clusters that originally had the cells
    const Int_t ncells = clus->GetMultiplicity();
    Int_t *digList     = clus->GetDigitsList();
    
    for (Int_t iLoopCell = 0 ; iLoopCell < ncells ; iLoopCell++)
    {
      AliEMCALDigit *digit = static_cast<AliEMCALDigit*>(fDigitsArr->At(digList[iLoopCell]));
      Int_t idCell = digit->GetId();
      
      if (idCell>=0)
      {
        Int_t idCluster = fOrgClusterCellId[idCell];
        Bool_t set = kTRUE;
        for (Int_t icl =0; icl < nClu; icl++)
        {
          if (((Int_t)clArray.GetAt(icl))==-1) continue;
          if (idCluster == ((Int_t)clArray.GetAt(icl))) set = kFALSE;
        }
        if (set && idCluster >= 0)
        {
          clArray.SetAt(idCluster,nClu++);
          nLabTotOrg+=(fEvent->GetCaloCluster(idCluster))->GetNLabels();
          
          //Search highest E cluster
          AliVCluster * clOrg = fEvent->GetCaloCluster(idCluster);
          if (emax < clOrg->E())
          {
            emax  = clOrg->E();
            idMax = idCluster;
          }
        }
      }
    }// cell loop
    
    // Put the first in the list the cluster with highest energy
    if (idMax != ((Int_t)clArray.GetAt(0))) // Max not at first position
    {
      Int_t maxIndex = -1;
      Int_t firstCluster = ((Int_t)clArray.GetAt(0));
      for (Int_t iLoopCluster = 0 ; iLoopCluster < nClu ; iLoopCluster++)
      {
        if (idMax == ((Int_t)clArray.GetAt(iLoopCluster))) maxIndex = iLoopCluster;
      }
      
      if (firstCluster >=0 && idMax >=0)
      {
        clArray.SetAt(idMax,0);
        clArray.SetAt(firstCluster,maxIndex);
      }
    }
    
    // Get the labels list in the original clusters, assign all to the new cluster
    TArrayI clMCArray(nLabTotOrg) ;
    clMCArray.Reset();
    
    Int_t nLabTot = 0;
    for (Int_t iLoopCluster = 0 ; iLoopCluster < nClu ; iLoopCluster++)
    {
      Int_t idCluster = (Int_t) clArray.GetAt(iLoopCluster);
      AliVCluster * clOrg = fEvent->GetCaloCluster(idCluster);
      Int_t nLab = clOrg->GetNLabels();
      
      for (Int_t iLab = 0 ; iLab < nLab ; iLab++)
      {
        Int_t lab = clOrg->GetLabelAt(iLab) ;
        if (lab>=0)
        {
          Bool_t set = kTRUE;
          for(Int_t iLabTot =0; iLabTot < nLabTot; iLabTot++)
          {
            if (lab == ((Int_t)clMCArray.GetAt(iLabTot))) set = kFALSE;
          }
          if (set) clMCArray.SetAt(lab,nLabTot++);
        }
      }
    }// cluster loop
    
    // Set the final list of labels to rec point
    
    Int_t *labels = new Int_t[nLabTot];
    for(Int_t il = 0; il < nLabTot; il++) labels[il] = clMCArray.GetArray()[il];
    clus->SetParents(nLabTot,labels);
    
  } // rec point array
}

//________________________________________________________________________________________
void AliEmcalCorrectionClusterizer::Init()
{
  // Select clusterization/unfolding algorithm and set all the needed parameters.
  
  if (fEvent->GetRunNumber()==fRun)
    return;
  fRun = fEvent->GetRunNumber();
  
  if (fJustUnfold){
    // init the unfolding afterburner
    delete fUnfolder;
    fUnfolder = new AliEMCALAfterBurnerUF(fRecParam->GetW0(),fRecParam->GetLocMaxCut(),fRecParam->GetMinECut());
    return;
  }
  
  if (fGeomName.Length()>0)
    fGeom = AliEMCALGeometry::GetInstance(fGeomName);
  else
    fGeom = AliEMCALGeometry::GetInstanceFromRunNumber(fRun);
  if (!fGeom) {
    AliFatal("Geometry not available!!!");
    return;
  }
  
  if (!fGeomMatrixSet) {
    if (fLoadGeomMatrices) {
      for(Int_t mod=0; mod < fGeom->GetNumberOfSuperModules(); ++mod) {
        if (fGeomMatrix[mod]){
          // AliDebug(3, fGeomMatrix[mod]); need to "print"
        fGeom->SetMisalMatrix(fGeomMatrix[mod],mod);
        }
      }
    } else { // get matrix from file (work around bug in aliroot)
      for(Int_t mod=0; mod < fGeom->GetEMCGeometry()->GetNumberOfSuperModules(); ++mod) {
        const TGeoHMatrix *gm = 0;
        if (fEsd) {
          gm = fEsd->GetEMCALMatrix(mod);
        } else {
          AliAODHeader *aodheader = dynamic_cast<AliAODHeader*>(fAod->GetHeader());
          if(!aodheader) AliFatal("Not a standard AOD");
          if (aodheader) {
            gm = aodheader->GetEMCALMatrix(mod);
          }
        }
        if (gm) {
          //AliDebug(3, gm); need to "print"
        fGeom->SetMisalMatrix(gm,mod);
        }
      }
    }
    fGeomMatrixSet=kTRUE;
  }
  
  // setup digit array if needed
  if (!fDigitsArr) {
    fDigitsArr = new TClonesArray("AliEMCALDigit", 1000);
    fDigitsArr->SetOwner(1);
  }
  
  // then setup clusterizer
  if (fClusterizer) {
    // avoid to delete digits array
    fClusterizer->SetDigitsArr(0);
    delete fClusterizer;
  }
  if (fRecParam->GetClusterizerFlag() == AliEMCALRecParam::kClusterizerv1)
    fClusterizer = new AliEMCALClusterizerv1(fGeom);
  else if (fRecParam->GetClusterizerFlag() == AliEMCALRecParam::kClusterizerNxN) {
    AliEMCALClusterizerNxN *clusterizer = new AliEMCALClusterizerNxN(fGeom);
    clusterizer->SetNRowDiff(fRecParam->GetNRowDiff()); //MV: already done in AliEMCALClusterizer::InitParameters
    clusterizer->SetNColDiff(fRecParam->GetNColDiff()); //MV: already done in AliEMCALClusterizer::InitParameters
    fClusterizer = clusterizer;
  }
  else if (fRecParam->GetClusterizerFlag() == AliEMCALRecParam::kClusterizerv2)
    fClusterizer = new AliEMCALClusterizerv2(fGeom);
  else if (fRecParam->GetClusterizerFlag() == AliEMCALRecParam::kClusterizerFW) {
    AliEMCALClusterizerFixedWindow *clusterizer = new AliEMCALClusterizerFixedWindow(fGeom);
    clusterizer->SetNphi(fNPhi);
    clusterizer->SetNeta(fNEta);
    clusterizer->SetShiftPhi(fShiftPhi);
    clusterizer->SetShiftEta(fShiftEta);
    clusterizer->SetTRUshift(fTRUShift);
    fClusterizer = clusterizer;
  }
  else {
    AliFatal(Form("Clusterizer < %d > not available", fRecParam->GetClusterizerFlag()));
  }
  fClusterizer->InitParameters(fRecParam);
  
  if ((!fCalibData&&fLoadCalib) || (!fPedestalData&&fLoadPed)) {
    AliCDBManager *cdb = AliCDBManager::Instance();
    if (!cdb->IsDefaultStorageSet() && !fOCDBpath.IsNull())
      cdb->SetDefaultStorage(fOCDBpath);
    if (fRun!=cdb->GetRun())
      cdb->SetRun(fRun);
  }
  if (!fCalibData&&fLoadCalib&&fRun>0) {
    AliCDBEntry *entry = static_cast<AliCDBEntry*>(AliCDBManager::Instance()->Get("EMCAL/Calib/Data"));
    if (entry)
      fCalibData =  static_cast<AliEMCALCalibData*>(entry->GetObject());
    if (!fCalibData)
      AliFatal("Calibration parameters not found in CDB!");
  }
  if (!fPedestalData&&fLoadPed&&fRun>0) {
    AliCDBEntry *entry = static_cast<AliCDBEntry*>(AliCDBManager::Instance()->Get("EMCAL/Calib/Pedestals"));
    if (entry)
      fPedestalData =  static_cast<AliCaloCalibPedestal*>(entry->GetObject());
  }
  if (fCalibData) {
    fClusterizer->SetInputCalibrated(kFALSE);
    fClusterizer->SetCalibrationParameters(fCalibData);
  } else {
    fClusterizer->SetInputCalibrated(kTRUE);
  }
  fClusterizer->SetCaloCalibPedestal(fPedestalData);
  fClusterizer->SetJustClusters(kTRUE);
  fClusterizer->SetDigitsArr(fDigitsArr);
  fClusterizer->SetOutput(0);
  fClusterArr = const_cast<TObjArray *>(fClusterizer->GetRecPoints());
  
}