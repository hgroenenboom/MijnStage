#include "overlapFFT.h"
#include <stdlib.h>
#include <time.h> 

overlapFFT::overlapFFT() {
}

overlapFFT::overlapFFT(dsp::FFT *fftFunctionP, int numOverlaps, int fftSize)
	: fft(fftSize),
	binDelay(fftSize, numOverlaps)
{
	this->fftFunctionP = fftFunctionP;
	this->numOverlaps = numOverlaps;
	this->fftSize = fftSize;

	//srand(time(NULL));
	//for (int i = 0; i < fftSize; i++) {
	//	fftDelays[i] = new ForwardCircularDelay(numOverlaps * (44100 / fftSize) * 2, i * 2);
	//}

	//replace all 512 values with bufferSize
	//if (fftSize > 512) { //A
		numFFTs = 1;
	//}
	//else if (512 >= fftSize) {			//B
	//	numFFTs = 512 / fftSize;
	//}	

	inputMemory = ForwardCircularDelay(5 * fftSize, 1 * fftSize, 1, true);
	DBG("InputMemory");
	outputMemory = ForwardCircularDelay(5 * fftSize, 1 * fftSize, 1, true);
	DBG("OutputMemory ");

	hanningWindow.resize(fftSize);
	createHanningWindow();
}

// [1]
void overlapFFT::pushDataIntoMemoryAndPerformFFTs(AudioSampleBuffer& buffer, int numSamples, int channel) {
	const float* channelData = buffer.getReadPointer(channel, 0);
	float* outBuffer = buffer.getWritePointer(channel, 0);
	this->channel = channel;

	for (int i = 0; i < numSamples; i++) {
		// [1]
		inputMemory.feedBack(0, 0.0f); 
		inputMemory.pushSingleValue(channelData[i]); //push new data into the inputMemory. The memory will shift it's own pointer.

		//count if enough samples are collected for the FFT calculations
		inputForFFTCounter++; 
		inputForFFTCounter %= fftSize;

		// [23467]
		if (inputForFFTCounter == 0) {
			runThroughFFTs(); // [2,3,4]
			adjustMemoryPointersAndClearOutputMemory(); // [6,7]
		}

		outBuffer[i] = getOutputData() * (1.0f / (numOverlaps / 2));	
	}
}

// [2,3,4]
void overlapFFT::runThroughFFTs() { // 234
	// run through all overlaps
	for (int ovLap = 0; ovLap < numOverlaps; ovLap++) {

		// calculate the indices for the current overlap
		startIndex = ovLap * (fftSize / numOverlaps);
		endIndex = startIndex + fftSize;

		// fill the fft buffer
		fillFFTBuffer(startIndex, endIndex); //[2]

		applyFFT(ovLap); //[3]

		pushFFTDataIntoOutputDelayBuffer(startIndex, endIndex); //[4]
	}

	//for (int i = 0; i < fftSize / 2; i++) {
	//	fftDelays[i]->adjustPointers();
	//}
}

// [2]
void overlapFFT::fillFFTBuffer(int startIndex, int endIndex)
{
	zeromem(fft.fftData, sizeof(fft.fftData));

	// replace with a single function
	for (int i = 0; i < fftSize; i++) {
		fft.fftData[i] = inputMemory.readValue(i + startIndex);
	}
}

// [3]
void overlapFFT::applyFFT(int ovLap) {
	//FFT
	applyHannningWindowToFftBuffer();

	for (int i = 0; i < fftSize; i++) {
		timeData[i]._Val[0] = fft.fftData[i];
		timeData[i]._Val[1] = 0.0f;
	}

	//256 complex vectors and 256 complex conjugates of the first 256 vectors
	// i.e. 256 positive cycloids and 256 negative cycloids.
	fftFunctionP->perform(timeData, spectralDataIn, false);

	for (int i = 0; i < fftSize; i++) {
		carToPol(&spectralDataIn[i]._Val[0], &spectralDataIn[i]._Val[1]);
		//if (i < 0.5 * fftSize) spectralDataIn[i]._Val[0] *= 2.0f;
		if (i == 0 || i > 0.5 * fftSize) spectralDataIn[i]._Val[0] = 0.0f; //set negative real values to 0.
		spectralDataOut[i] = spectralDataIn[i];
	}


	//MODIFICATIONS	
	binDelay.getOutputMagnitudes(spectralDataOut);
	binDelay.pushMagnitudesIntoDelay(spectralDataIn);
	binDelay.adjustPointers();

	for (int i = fftSize / 2; i < fftSize; i++) {
		spectralDataOut[i]._Val[0] = 0.0f;
	}

	/*
	switch (channel) {
	case 0:
	case 1:
	}
	*/

	////IFFT
	for (int i = 0; i < fftSize; i++) 
		polToCar(&spectralDataOut[i]._Val[0], &spectralDataOut[i]._Val[1]);

	fftFunctionP->perform(spectralDataOut, timeData, true);

	for (int i = 0; i < fftSize; i++) {
		fft.fftData[i] = timeData[i].real();
	}

	//Apply hanningWindow
	applyHannningWindowToFftBuffer();
}

// [3.1]
void overlapFFT::applyHannningWindowToFftBuffer() {
	for (int i = 0; i < fftSize; i++) { //run through each input amplitude value.
		fft.fftData[i] *= hanningWindow[i];
	}
}

// [3.2]
void overlapFFT::carToPol(float* inReOutM, float* inImOutPhi) {
	float re = *inReOutM;
	float im = *inImOutPhi;

	// phytagoras calculation
	*inReOutM = pow(pow(re, 2.0f) + pow(im, 2.0f), 0.5f);
	*inImOutPhi = atan2(im, re);
}

// [3.3]
void overlapFFT::polToCar(float* inMOutRe, float* inPhiOutIm) {
	float mag = *inMOutRe;
	float phi = *inPhiOutIm;

	*inMOutRe = mag * cos(phi);
	*inPhiOutIm = mag * sin(phi);
}

// [3.4]
void overlapFFT::applyHannningWindowToFftAudio() {
	for (int i = 0; i < fftSize; i++) { //run through each magnitude value.
		fft.fftData[i * 2] *= hanningWindow[i];
	}
}

// [4]
void overlapFFT::pushFFTDataIntoOutputDelayBuffer(int startIndex, int endIndex) {
	for (int i = 0; i < fftSize; i++) {
		outputMemory.addValue(fft.fftData[i], i + startIndex);
	}
}

// [5]
float overlapFFT::getOutputData() {
	return outputMemory.readValue(inputForFFTCounter);
}

// [6, 7]
void overlapFFT::adjustMemoryPointersAndClearOutputMemory() {
	outputMemory.clearBufferData(-1 * fftSize, 0);

	inputMemory.adjustPointers(512);
	outputMemory.adjustPointers(512);
}

// Initialize the hanning window
void overlapFFT::createHanningWindow() {
	for (int i = 0; i < fftSize; i++) {
		hanningWindow[i] = sin((float(i) / fftSize) * float_Pi);
	}
}