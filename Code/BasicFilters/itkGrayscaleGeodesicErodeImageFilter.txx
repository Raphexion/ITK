/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    itkGrayscaleGeodesicErodeImageFilter.txx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __itkGrayscaleGeodesicErodeImageFilter_txx
#define __itkGrayscaleGeodesicErodeImageFilter_txx

#include <limits.h>

#include "itkZeroFluxNeumannBoundaryCondition.h"
#include "itkNumericTraits.h"
#include "itkGrayscaleGeodesicErodeImageFilter.h"
#include "itkNeighborhoodAlgorithm.h"
#include "itkProgressReporter.h"
#include "itkImageRegionIterator.h"
#include "itkImageRegionConstIterator.h"
#include "itkConstShapedNeighborhoodIterator.h"


namespace itk {

template <class TInputImage, class TOutputImage>
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::GrayscaleGeodesicErodeImageFilter()
  : m_RunOneIteration( false ),  // run to convergence
    m_NumberOfIterationsUsed( 0 )
{
  this->SetNumberOfRequiredInputs(2);
}


template <class TInputImage, class TOutputImage>
void 
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::SetMarkerImage(const MarkerImageType* markerImage)
{
  // Process object is not const-correct so the const casting is required.
  this->SetNthInput(0, const_cast<MarkerImageType *>( markerImage ));
}


template <class TInputImage, class TOutputImage>
const typename GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>::MarkerImageType *
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::GetMarkerImage()
{
  return this->GetInput(0);
}


template <class TInputImage, class TOutputImage>
void 
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::SetMaskImage(const MaskImageType* maskImage)
{
  // Process object is not const-correct so the const casting is required.
  this->SetNthInput(1, const_cast<MaskImageType *>( maskImage ));
}


template <class TInputImage, class TOutputImage>
const typename GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>::MaskImageType *
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::GetMaskImage()
{
  return this->GetInput(1);
}


template <class TInputImage, class TOutputImage>
void 
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::GenerateInputRequestedRegion()
{
  // call the superclass' implementation of this method
  Superclass::GenerateInputRequestedRegion();
  
  // get pointers to the inputs
  MarkerImagePointer  markerPtr =
    const_cast< MarkerImageType * >( this->GetInput(0) );

  MaskImagePointer  maskPtr = 
    const_cast< MaskImageType * >( this->GetInput(1) );
  
  if ( !markerPtr || !maskPtr )
    {
    return;
    }

  // If configured to run one iteration, the marker image must be
  // padded by one pixel while the mask image can match the size of
  // the output.  If configured to run until convergence, the entire
  // marker and mask image will be required.
  //
  if (m_RunOneIteration)
    {
    // by calling the superclass' implementation above, the mask image
    // is already configured to have its requested input match the
    // output requested region. so nothing needs to be done for the
    // mask image.

    // the marker image needs to be padded by one pixel and cropped to
    // the LargestPossibleRegion.
    //
    
    // get a copy of the marker image requested region (should equal
    // the output requested region)
    MarkerImageRegionType markerRequestedRegion;
    markerRequestedRegion = markerPtr->GetRequestedRegion();

    // pad the marker requested region by the elementary operator radius
    markerRequestedRegion.PadByRadius( 1 );

    // crop the marker requested region at the marker's largest possible region
    if ( markerRequestedRegion.Crop(markerPtr->GetLargestPossibleRegion()) )
      {
      markerPtr->SetRequestedRegion( markerRequestedRegion );
      return;
      }
    else
      {
      // Couldn't crop the region (requested region is outside the largest
      // possible region).  Throw an exception.
      
      // store what we tried to request (prior to trying to crop)
      markerPtr->SetRequestedRegion( markerRequestedRegion );
    
      // build an exception
      InvalidRequestedRegionError e(__FILE__, __LINE__);
      OStringStream msg;
      msg << static_cast<const char *>(this->GetNameOfClass())
          << "::GenerateInputRequestedRegion()";
      e.SetLocation(msg.str().c_str());
      e.SetDescription("Requested region for the marker image is (at least partially) outside the largest possible region.");
      e.SetDataObject(markerPtr);
      throw e;
      }
    }
  else
    {
    // The filter was configured to run to convergence.  We need to
    // configure the inputs such that all the data is available.
    //
    markerPtr->SetRequestedRegion(markerPtr->GetLargestPossibleRegion());
    maskPtr->SetRequestedRegion(maskPtr->GetLargestPossibleRegion());
    }
}


template <class TInputImage, class TOutputImage>
void 
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::EnlargeOutputRequestedRegion(DataObject *)
{
  // if running to convergence, then all the output will be produced
  if ( !m_RunOneIteration)
    {
    this->GetOutput()
      ->SetRequestedRegion( this->GetOutput()->GetLargestPossibleRegion() );
    }
}


template<class TInputImage, class TOutputImage>
void
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::GenerateData()
{
  // If the filter is configured to run a single iteration, then
  // delegate to the superclass' version of GenerateData. This will
  // engage the multithreaded version of the GenerateData.
  //
  if (m_RunOneIteration)
    {
    Superclass::GenerateData();
    m_NumberOfIterationsUsed = 1;
    return;
    }


  //ProgressReporter progress(this, 0, ????);


  // Filter was configured to run until convergence. We need to
  // delegate to an instance of the filter to run each iteration
  // separately. For efficiency, we will delegate to an instance that
  // is templated over <TInputImage, TInputImage> to avoid any
  // pixelwise casting until the final output image is configured.
  typename GrayscaleGeodesicErodeImageFilter<TInputImage, TInputImage>::Pointer
    singleIteration
         = GrayscaleGeodesicErodeImageFilter<TInputImage, TInputImage>::New();
  bool done = false;

  // set up the singleIteration filter. we are not using the grafting
  // mechanism because we only need the requested regioN to be set up
  singleIteration->RunOneIterationOn();
  singleIteration->SetMarkerImage( this->GetMarkerImage() );
  singleIteration->SetMaskImage( this->GetMaskImage() );
  singleIteration->GetOutput()
    ->SetRequestedRegion( this->GetOutput()->GetRequestedRegion() );
  
  // run until convergence
  while (!done)
    {
    // run one iteration
    singleIteration->Update();

    // Check for convergence.  Compare the output of the single
    // iteration of the algorithm with the current marker image.
    ImageRegionConstIterator<TInputImage>
      singleInIt(singleIteration->GetMarkerImage(),
                 singleIteration->GetOutput()->GetRequestedRegion() );
    ImageRegionIterator<TInputImage>
      singleOutIt(singleIteration->GetOutput(),
                  singleIteration->GetOutput()->GetRequestedRegion() );

    done = true;
    while ( !singleOutIt.IsAtEnd() )
      {
      // exit early from check on first pixel that is different
      if (singleInIt.Get() != singleOutIt.Get())
        {
        done = false;
        break;        // exit early from check
        }
      ++singleInIt;
      ++singleOutIt;
      }

    // If we have not converged, then setup the singleIteration filter
    // for the next iteration.
    //
    if (!done)
      {
      // disconnect the current output from the singleIteration object
      MarkerImagePointer marker = singleIteration->GetOutput();
      marker->DisconnectPipeline();
      // assign the old output as the input
      singleIteration->SetMarkerImage( marker ); 
      // since DisconnectPipeline() creates a new output object, we need
      // to regraft the information onto the output
      singleIteration->GetOutput()
        ->SetRequestedRegion( this->GetOutput()->GetRequestedRegion() );

      // Keep track of how many iterations have be done
      m_NumberOfIterationsUsed++;
      }
    }

  // Convert the output of singleIteration to an TOutputImage type
  // (could use a CastImageFilter here to thread the copy)
  typename OutputImageType::Pointer outputPtr = this->GetOutput();
  outputPtr->SetBufferedRegion( outputPtr->GetRequestedRegion() );
  outputPtr->Allocate();

  // walk the output of the singleIteration
  ImageRegionIterator<TInputImage> singleIt(singleIteration->GetOutput(),
                                            outputPtr->GetRequestedRegion() );
  // walk the real output of the filter
  ImageRegionIterator<TOutputImage> outIt(outputPtr,
                                          outputPtr->GetRequestedRegion());

  // cast and copy
  while( !outIt.IsAtEnd() )
    {
    outIt.Set( static_cast<OutputImagePixelType>( singleIt.Get() ) );
    ++outIt;
    ++singleIt;
    }
}



template<class TInputImage, class TOutputImage>
void
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::ThreadedGenerateData(const OutputImageRegionType& outputRegionForThread,
                       int threadId) 
{
  // Set up the progress reporter
  ProgressReporter progress(this, threadId,
                            outputRegionForThread.GetNumberOfPixels());
  
  // Set up the boundary condition to have no upwind derivatives
  ZeroFluxNeumannBoundaryCondition<TInputImage> BC;

  // Neighborhood iterator.  Let's use a shaped neighborhood so we can
  // restrict the access to face connected neighbors. This iterator
  // will be applied to the marker image.
  typedef ConstShapedNeighborhoodIterator<TInputImage> NeighborhoodIteratorType;

  // iterator for the marker image
  // NeighborhoodIteratorType markerIt;

  // iterator for the mask image
  ImageRegionConstIterator<TInputImage> maskIt; 

  // output iterator
  ImageRegionIterator<TOutputImage> oIt;
 
  
  // Find the boundary "faces". Structuring element is elementary
  // (face connected neighbors within a radius of 1).
  typename NeighborhoodAlgorithm::ImageBoundaryFacesCalculator<MarkerImageType>::FaceListType faceList;
  typename NeighborhoodAlgorithm::ImageBoundaryFacesCalculator<MarkerImageType> fC;
  typename NeighborhoodAlgorithm::ImageBoundaryFacesCalculator<MarkerImageType>::RadiusType kernelRadius;
  kernelRadius.Fill(1);
  faceList = fC(this->GetMarkerImage(), outputRegionForThread, kernelRadius);

  typename NeighborhoodAlgorithm::ImageBoundaryFacesCalculator<MarkerImageType>::FaceListType::iterator fit;

  unsigned int d;
  typename NeighborhoodIteratorType::OffsetValueType i;
  typename NeighborhoodIteratorType::OffsetType offset;

  MarkerImagePixelType value, erodeValue, maskValue;

  // Iterate over the faces
  //
  for (fit = faceList.begin(); fit != faceList.end(); ++fit)
    { 
    NeighborhoodIteratorType markerIt = NeighborhoodIteratorType(kernelRadius,
                                        this->GetMarkerImage(), *fit);
    maskIt=ImageRegionConstIterator<MaskImageType>(this->GetMaskImage(), *fit);
    oIt = ImageRegionIterator<OutputImageType>(this->GetOutput(), *fit);

    markerIt.OverrideBoundaryCondition(&BC);
    markerIt.GoToBegin();

    // setup the marker iterator to only visit face connected
    // neighbors and the center pixel
    offset.Fill(0);
    markerIt.ActivateOffset(offset); // center pixel
    for (d=0; d < InputImageType::ImageDimension; ++d)
      {
      for (i=-1; i<=1; i+=2)
        {
        offset[d] = i;
        markerIt.ActivateOffset(offset); // a neighbor pixel in dimension d
        }
      offset[d] = 0;
      }

    // iterate over image region
    while ( ! oIt.IsAtEnd() )
      {
      erodeValue = NumericTraits<MarkerImagePixelType>::max();
      
      // Erode by checking the face connected neighbors (and center pixel)
      typename NeighborhoodIteratorType::ConstIterator sIt;
      for (sIt = markerIt.Begin(); !sIt.IsAtEnd(); sIt++)
        {
        // a pixel in the neighborhood
        value = sIt.Get();

        // erosion is a min operation
        if (value < erodeValue)
          {
          erodeValue = value;
          }
        }
      
      // Mask operation.  For geodesic erosion, the mask operation is
      // a pixelwise max operator with the elementary eroded image and
      // the mask image
      maskValue = maskIt.Get();

      if (maskValue > erodeValue)
        {
        erodeValue = maskValue;
        }

      // set the output pixel value
      oIt.Set( static_cast<OutputImagePixelType>( erodeValue ) );

      // move to next pixel
      ++oIt;
      ++markerIt;
      ++maskIt;

      progress.CompletedPixel();
      }
    }
}



template<class TInputImage, class TOutputImage>
void
GrayscaleGeodesicErodeImageFilter<TInputImage, TOutputImage>
::PrintSelf(std::ostream &os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);

  os << indent << "Run one iteration: " << (m_RunOneIteration ? "on" : "off")
     << std::endl;
  os << indent << "Number of iterations used to produce current output: "
     << m_NumberOfIterationsUsed << std::endl;
}
  
}// end namespace itk
#endif
