/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2016 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "FileReader.h"
#include "FileReaderEditor.h"
#include <stdio.h>
#include "../../AccessClass.h"
#include "../PluginManager/PluginManager.h"


FileReader::FileReader()
    : GenericProcessor ("File Reader")
    , timestamp             (0)
    , currentSampleRate     (0)
    , currentNumChannels    (0)
    , currentSample         (0)
    , currentNumSamples     (0)
    , startSample           (0)
    , stopSample            (0)
    , counter               (0)
{
    setProcessorType (PROCESSOR_TYPE_SOURCE);

    setEnabledState (false);

    const int numFileSources = AccessClass::getPluginManager()->getNumFileSources();
    for (int i = 0; i < numFileSources; ++i)
    {
        Plugin::FileSourceInfo info = AccessClass::getPluginManager()->getFileSourceInfo (i);

        StringArray extensions;
        extensions.addTokens (info.extensions, ";", "\"");

        const int numExtensions = extensions.size();
        for (int j = 0; j < numExtensions; ++j)
        {
            supportedExtensions.set (extensions[j].toLowerCase(), i + 1);
        }
    }
    
    /*
    const DataChannel* in = getDataChannel(0);
    EventChannel* ev = new EventChannel(EventChannel::TTL, 8, 1, (in) ? in->getSampleRate() : CoreServices::getGlobalSampleRate(), this);
    moduleEventChannels.add(ev);
*/
}


FileReader::~FileReader()
{
}


AudioProcessorEditor* FileReader::createEditor()
{
    editor = new FileReaderEditor (this, true);

    return editor;
}


bool FileReader::isReady()
{
    if (! input)
    {
        CoreServices::sendStatusMessage ("No file selected in File Reader.");
        return false;
    }
    else
    {
        return input->isReady();
    }
}


float FileReader::getDefaultSampleRate() const
{
    if (input)
        return currentSampleRate;
    else
        return 44100.0;
}


int FileReader::getDefaultNumDataOutputs(DataChannel::DataChannelTypes type, int subproc) const
{
	if (subproc != 0) return 0;
	if (type != DataChannel::HEADSTAGE_CHANNEL) return 0;
    if (input)
        return currentNumChannels;
    else
        return 16;
}


float FileReader::getBitVolts (const DataChannel* chan) const
{
    if (input)
        return chan->getBitVolts();
    else
        return 0.05f;
}


void FileReader::setEnabledState (bool t)
{
    isEnabled = t;
}


bool FileReader::isFileSupported (const String& fileName) const
{
    const File file (fileName);
    String ext = file.getFileExtension().toLowerCase().substring (1);

    return isFileExtensionSupported (ext);
}


bool FileReader::isFileExtensionSupported (const String& ext) const
{
    const int index = supportedExtensions[ext] - 1;
    const bool isExtensionSupported = index >= 0;

    return isExtensionSupported;
}


bool FileReader::setFile (String fullpath)
{
    File file (fullpath);

    String ext = file.getFileExtension().toLowerCase().substring (1);
    const int index = supportedExtensions[ext] - 1;
    const bool isExtensionSupported = index >= 0;

    if (isExtensionSupported)
    {
        const int index = supportedExtensions[ext] - 1;
        Plugin::FileSourceInfo sourceInfo = AccessClass::getPluginManager()->getFileSourceInfo (index);
        input = sourceInfo.creator();
    }
    else
    {
        CoreServices::sendStatusMessage ("File type not supported");
        return false;
    }

    if (! input->OpenFile (file))
    {
        input = nullptr;
        CoreServices::sendStatusMessage ("Invalid file");

        return false;
    }

    const bool isEmptyFile = input->getNumRecords() <= 0;
    if (isEmptyFile)
    {
        input = nullptr;
        CoreServices::sendStatusMessage ("Empty file. Inoring open operation");

        return false;
    }

    static_cast<FileReaderEditor*> (getEditor())->populateRecordings (input);
    setActiveRecording (0);

    return true;
}


void FileReader::setActiveRecording (int index)
{
    input->setActiveRecord (index);

    currentNumChannels  = input->getActiveNumChannels();
    currentNumSamples   = input->getActiveNumSamples();
    currentSampleRate   = input->getActiveSampleRate();

    currentSample   = 0;
    startSample     = 0;
    stopSample      = currentNumSamples;

    for (int i = 0; i < currentNumChannels; ++i)
    {
        channelInfo.add (input->getChannelInfo (i));
    }

    static_cast<FileReaderEditor*> (getEditor())->setTotalTime (samplesToMilliseconds (currentNumSamples));

    readBuffer.malloc (currentNumChannels * BUFFER_SIZE);
}


String FileReader::getFile() const
{
    if (input)
        return input->getFileName();
    else
        return String::empty;
}


void FileReader::updateSettings()
{
     if (!input) return;

     for (int i=0; i < currentNumChannels; i++)
     {
         dataChannelArray[i]->setBitVolts(channelInfo[i].bitVolts);
         dataChannelArray[i]->setName(channelInfo[i].name);
     }
}


void FileReader::process (AudioSampleBuffer& buffer)
{
    const int samplesNeeded = int (float (buffer.getNumSamples()) * (getDefaultSampleRate() / 44100.0f));
    // FIXME: needs to account for the fact that the ratio might not be an exact
    //        integer value
    int samplesRead = 0;

    while (samplesRead < samplesNeeded)
    {
        count +=1;
        int samplesToRead = samplesNeeded - samplesRead;
        int iterationsPerSecond = getDefaultSampleRate()/samplesToRead;
        int sample = getDefaultSampleRate()-iterationsPerSecond*samplesToRead;
        //std::cout<<"sample: " <<sample<<"\n";
        if ( (currentSample + samplesToRead) > stopSample)
        {
            samplesToRead = stopSample - currentSample;
            if (samplesToRead > 0)
                input->readData (readBuffer + samplesRead, samplesToRead);

            input->seekTo (startSample);
            currentSample = startSample;
        }
        else
        {
            input->readData (readBuffer + samplesRead, samplesToRead);
            currentSample += samplesToRead;
        }
        //std::cout<<"samplesRead: " << samplesRead << "\n";
        //std::cout<<"mod operation: " << count % iterationsPerSecond << "\n";
        if (count % iterationsPerSecond == 0){
            int *evntData= 0;
            TTLEventPtr event = TTLEvent::createTTLEvent(moduleEventChannels[0], getTimestamp(0),
                                                    &evntData, sizeof(int), 0);
            //TTLEvent::createTTLEvent(<#const EventChannel *channelInfo#>, <#int64 timestamp#>, <#const void *eventData#>, <#int dataSize#>, <#uint16 channel#>)
            addEvent(moduleEventChannels[0], event, sample);
            //addEvent(<#int channelIndex#>, <#const Event *event#>, <#int sampleNum#>)
        }
        
        samplesRead += samplesToRead;
    }

    for (int i = 0; i < currentNumChannels; ++i)
    {
        input->processChannelData (readBuffer, buffer.getWritePointer (i, 0), i, samplesNeeded);
    }
    
    timestamp += samplesNeeded;
	setTimestampAndSamples(timestamp, samplesNeeded);
    
}


void FileReader::setParameter (int parameterIndex, float newValue)
{
    switch (parameterIndex)
    {
        //Change selected recording
        case 0:
            setActiveRecording (newValue);
            break;

        //set startTime
        case 1: 
            startSample = millisecondsToSamples (newValue);
            currentSample = startSample;

            static_cast<FileReaderEditor*> (getEditor())->setCurrentTime (samplesToMilliseconds (currentSample));
            break;

        //set stop time
        case 2:
            stopSample = millisecondsToSamples(newValue);
            currentSample = startSample;

            static_cast<FileReaderEditor*> (getEditor())->setCurrentTime (samplesToMilliseconds (currentSample));
            break;
    }
}


unsigned int FileReader::samplesToMilliseconds (int64 samples) const
{
    return (unsigned int) (1000.f * float (samples) / currentSampleRate);
}


int64 FileReader::millisecondsToSamples (unsigned int ms) const
{
    return (int64) (currentSampleRate * float (ms) / 1000.f);
}

void FileReader::createEventChannels(){
    
    moduleEventChannels.clear();
   
        const DataChannel* in = getDataChannel(0);
        EventChannel* ev = new EventChannel(EventChannel::TTL, 8, 1, (in) ? in->getSampleRate() : CoreServices::getGlobalSampleRate(), this);
    
        ev->setName("regular file reader output ");
        ev->setDescription("Triggers about every second");
        String identifier = "secondly.reader.";
        String typeDesc = "secondly";
        ev->setIdentifier(identifier);
        //MetaDataDescriptor md(MetaDataDescriptor::CHAR, 34, "Phase Type", "Description of the phase condition", "channelInfo.extra");
        //MetaDataValue mv(md);
        //mv.setValue(typeDesc);
        eventChannelArray.add(ev);
        moduleEventChannels.add(ev);
}
