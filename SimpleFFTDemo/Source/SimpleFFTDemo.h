/*
  ==============================================================================

   This file is part of the JUCE examples.
   Copyright (c) 2017 - ROLI Ltd.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   To use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES,
   WHETHER EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR
   PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

/*******************************************************************************
 The block below describes the properties of this PIP. A PIP is a short snippet
 of code that can be read by the Projucer and used to generate a JUCE project.

 BEGIN_JUCE_PIP_METADATA

 name:             SimpleFFTDemo
 version:          1.0.0
 vendor:           JUCE
 website:          http://juce.com
 description:      Simple FFT application.

 dependencies:     juce_audio_basics, juce_audio_devices, juce_audio_formats,
                   juce_audio_processors, juce_audio_utils, juce_core,
                   juce_data_structures, juce_dsp, juce_events, juce_graphics,
                   juce_gui_basics, juce_gui_extra
 exporters:        xcode_mac, vs2017, linux_make, androidstudio, xcode_iphone

 moduleFlags:      JUCE_STRICT_REFCOUNTEDPOINTER=1

 type:             Component
 mainClass:        SimpleFFTDemo

 useLocalCopy:     1

 END_JUCE_PIP_METADATA

*******************************************************************************/

#pragma once


//==============================================================================
class SimpleFFTDemo   : public AudioAppComponent,
                        private Timer
{
public:
    SimpleFFTDemo() :
         #ifdef JUCE_DEMO_RUNNER
          AudioAppComponent (getSharedAudioDeviceManager (1, 0)),
         #endif
          forwardFFT (fftOrder),
          spectrogramImage (Image::RGB, 512, 512, true)
    {
        setOpaque (true);

       #ifndef JUCE_DEMO_RUNNER
        RuntimePermissions::request (RuntimePermissions::recordAudio,
                                     [this] (bool granted)
                                     {
                                         int numInputChannels = granted ? 2 : 0;
                                         setAudioChannels (numInputChannels, 2);
                                     });
       #else
        setAudioChannels (2, 2);
       #endif

        startTimerHz (60);
        setSize (700, 500);
    }

    ~SimpleFFTDemo()
    {
        shutdownAudio();
    }

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double newSampleRate   ) override
    {
        mpScopeBuffer.reset( new AudioSampleBuffer(1,samplesPerBlockExpected));
        mpLastAudioBuffer.reset( new AudioSampleBuffer(1,samplesPerBlockExpected));
        mpScopeBuffer.get()->clear();
        mpLastAudioBuffer.get()->clear();
    }

    void releaseResources() override
    {
        mpScopeBuffer.reset(nullptr);
        mpLastAudioBuffer.reset(nullptr);
    }

    void getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill) override
    {
        if (bufferToFill.buffer->getNumChannels() > 0)
        {
            const auto* channelData = bufferToFill.buffer->getReadPointer (0, bufferToFill.startSample);

            for (auto i = 0; i < bufferToFill.numSamples; ++i)
                pushNextSampleIntoFifo (channelData[i]);
            
            // here we sum our two inputs into a single channel buffer
            sumCopyBuffer(bufferToFill.buffer);
        }
    }

    //==============================================================================
    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black);

        g.setOpacity (1.0f);
        auto area = getLocalBounds();
        auto leftR = area.removeFromLeft(area.getWidth()>>1);
        auto rightR = area;
        g.drawImage (spectrogramImage,rightR.toFloat());
        drawScopeInContext(g,leftR);
    }

    void timerCallback() override
    {
        if (nextFFTBlockReady)
        {
            drawNextLineOfSpectrogram();
            nextFFTBlockReady = false;
            repaint();
        }
//        drawScope();
    }

    void pushNextSampleIntoFifo (float sample) noexcept
    {
        // if the fifo contains enough data, set a flag to say
        // that the next line should now be rendered..
        if (fifoIndex == fftSize)
        {
            if (! nextFFTBlockReady)
            {
                zeromem (fftData, sizeof (fftData));
                memcpy (fftData, fifo, sizeof (fifo));
                nextFFTBlockReady = true;
            }

            fifoIndex = 0;
        }

        fifo[fifoIndex++] = sample;
    }

    void drawNextLineOfSpectrogram()
    {
        auto rightHandEdge = spectrogramImage.getWidth() - 1;
        auto imageHeight   = spectrogramImage.getHeight();

        // first, shuffle our image leftwards by 1 pixel..
        spectrogramImage.moveImageSection (0, 0, 1, 0, rightHandEdge, imageHeight);

        // then render our FFT data..
        forwardFFT.performFrequencyOnlyForwardTransform (fftData);

        // find the range of values produced, so we can scale our rendering to
        // show up the detail clearly
        auto maxLevel = FloatVectorOperations::findMinAndMax (fftData, fftSize / 2);

        for (auto y = 1; y < imageHeight; ++y)
        {
            auto skewedProportionY = 1.0f - std::exp (std::log (y / (float) imageHeight) * 0.2f);
            auto fftDataIndex = jlimit (0, fftSize / 2, (int) (skewedProportionY * fftSize / 2));
            auto level = jmap (fftData[fftDataIndex], 0.0f, jmax (maxLevel.getEnd(), 1e-5f), 0.0f, 1.0f);

            spectrogramImage.setPixelAt (rightHandEdge, y, Colour::fromHSV (level, 1.0f, level, 1.0f));
        }
    }

    enum
    {
        fftOrder = 10,
        fftSize  = 1 << fftOrder
    };

private:
    void drawScopeInContext(Graphics& g, Rectangle<int> bounds) {
        auto imageHeight= bounds.getHeight();
        auto imageWidth = bounds.getWidth();

        copyScopeBuffer(); // get the latest audio data
        auto nSamps = mpScopeBuffer.get()->getNumSamples();
        float inc = (float)nSamps / (float)imageWidth;
        float count = 0;
        float xPos = 0.;
        float halfHeight = (float)imageHeight/2.0f;
        float yPos = halfHeight;
        int idx = 0;
        // erase
        g.setColour(Colours::darkgrey);
        g.fillRect(bounds);
        //
        g.setColour(Colours::orange);
        const float* buf = mpScopeBuffer.get()->getReadPointer(0);
        for (auto x = 1; x<imageWidth; ++x) {
            float y = 0.f;
            if (idx < nSamps) {
                y = halfHeight + (buf[idx] * halfHeight);
            }
            count += inc;
            idx = floor(count);
            g.drawLine(xPos,yPos,x,y);
            xPos = x;
            yPos = y;
        }
    }

    void sumCopyBuffer(AudioSampleBuffer* buffer) {
        const ScopedLock sl (lock);
        for( auto chan = 0; chan<buffer->getNumChannels(); ++chan) {
            if( 0==chan) {
                mpLastAudioBuffer.get()->copyFrom(0,
                                                  0,
                                                  *buffer,
                                                  0,
                                                  0,
                                                  buffer->getNumSamples());
            } else {
                mpLastAudioBuffer.get()->addFrom(0,
                                                 0,
                                                 *buffer,
                                                 chan,
                                                 0,
                                                 buffer->getNumSamples());
            }
        }
    }
    
    void copyScopeBuffer(void) {
        const ScopedLock sl (lock);
        mpScopeBuffer.get()->copyFrom(  0,
                                        0,
                                        *mpLastAudioBuffer.get(),
                                        0,
                                        0,
                                        mpLastAudioBuffer.get()->getNumSamples());
    }



    dsp::FFT forwardFFT;
    Image spectrogramImage;

    float fifo [fftSize];
    float fftData [2 * fftSize];
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;
    
    // oscilloscope...
    CriticalSection lock;
    std::unique_ptr<AudioSampleBuffer>   mpLastAudioBuffer;     // 1 channel, summed
    std::unique_ptr<AudioSampleBuffer>   mpScopeBuffer;         // 1 channel

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleFFTDemo)
};
