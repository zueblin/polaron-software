#include <SD.h>
#include "ProjectPersistence.h"
#include "ArduinoJson-v6.11.0.h"
#include "Sequencer.h"
#include "ParameterSet.h"
#include "Arduino.h"

#define PROJECTSLOTS 16

void ProjectPersistence::init(){
    int attempts = 4;
    while (!sdCardInitialized && attempts-- > 0) {
        sdCardInitialized = SD.begin(BUILTIN_SDCARD);
        Serial.println(F("Failed to initialize SD library"));
        delay(1000);
    }
    if (sdCardInitialized){
        Serial.println(F("SD lib initialized"));
        updateProjectList();
    } else {
        Serial.println(F("Failed to initialize SD library, giving up"));
    }
}

void ProjectPersistence::updateProjectList(){
    existingProjects = 0;
    for (int i = 0; i < PROJECTSLOTS; i++){
        char* filename = new char[20];
        sprintf(filename, "/p_%i.txt", i);
        if (SD.exists(filename)){
            existingProjects |= _BV(i);
        }
    }
}

void ProjectPersistence::save(int projectNum, Sequencer * sequencer){
    char* filename = new char[20];
    sprintf(filename, "/p_%i.txt", projectNum);
    // Serial.print(F("Storing to file: "));
    // Serial.println(filename);
    // Delete existing file, otherwise the configuration is appended to the file
    SD.remove(filename);

    // Open file for writing
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println(F("Failed to create file"));
        return;
    }
    file.print("{\"global\":");
    StaticJsonDocument<200> clockDoc;
    JsonObject clock = clockDoc.to<JsonObject>();
    clock["stepLength"] = sequencer->clock.getStepLength();
    clock["swing"] = sequencer->clock.getSwing();
    if (serializeJson(clockDoc, file) == 0) {
        Serial.println(F("Failed to write to file"));
    }
    file.print(",");

    // we serialize track by track in order to save memory.
    file.print("\"tracks\":[");
    for (int t = 0; t < NUMBER_OF_INSTRUMENTTRACKS; t++){
        StaticJsonDocument<40000> trackDoc;
        JsonObject track = trackDoc.to<JsonObject>();
        track["output1Gain"] = sequencer->audioChannels[t]->getOutput1Gain();
        track["output2Gain"] = sequencer->audioChannels[t]->getOutput2Gain();
        JsonArray patterns = track.createNestedArray("patterns");
        for (int p = 0; p < NUMBER_OF_PATTERNS; p++){
            JsonObject pattern = patterns.createNestedObject();
            SequencerPattern & sequencerPattern = sequencer->tracks[t].patterns[p];
            pattern["triggerState"] = sequencerPattern.triggerState;
            pattern["pLockArmState"] = sequencerPattern.pLockArmState;
            pattern["offset"] = sequencerPattern.offset;
            pattern["trackLength"] = sequencerPattern.trackLength;
            pattern["autoMutate"] = sequencerPattern.autoMutate;
            JsonArray steps = pattern.createNestedArray("steps");
            for (int s = 0; s < NUMBER_OF_STEPS_PER_PATTERN; s++){
                JsonObject step = steps.createNestedObject();
                SequencerStep & sequencerStep = sequencerPattern.steps[s];
                step["triggerMask"] = sequencerStep.triggerMask;
                JsonArray stepParams = step.createNestedArray("params");
                ParameterSet params = sequencerStep.params;
                stepParams.add(params.parameter1);
                stepParams.add(params.parameter2);
                stepParams.add(params.parameter3);
                stepParams.add(params.parameter4);
                stepParams.add(params.parameter5);
                stepParams.add(params.parameter6);
            }
        }
        if (serializeJson(trackDoc, file) == 0) {
            Serial.println(F("Failed to write to file"));
        }
        if (t < NUMBER_OF_INSTRUMENTTRACKS - 1){
            file.print(",");
        }
    }
    file.print("]}");
    // Close the file
    file.close();
    updateProjectList();
    Serial.println(F("Finished save"));
};

void ProjectPersistence::load(int projectNum, Sequencer * sequencer){
    char* filename = new char[20];
    sprintf(filename, "/p_%i.txt", projectNum);
    // Open file for writing
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        Serial.println(F("Failed to read file"));
        return;
    }
    if (file.find("\"global\":")){
        StaticJsonDocument<200> clockDoc;
        DeserializationError err = deserializeJson(clockDoc, file);
        if (err) {
            Serial.print(F("deserializeJson() returned "));
            Serial.println(err.c_str());
            return;
        }
        sequencer->clock.setStepLength(clockDoc["stepLength"]);
        sequencer->clock.setSwing(clockDoc["swing"]);
    } else {
        file.seek(0);
    };

    file.find("\"tracks\":[");
    int t = 0;
    int p = 0;
    int s = 0;
    do {
        StaticJsonDocument<48000> trackDoc;
        DeserializationError err = deserializeJson(trackDoc, file);
        // Parse succeeded?
        if (err) {
            Serial.print(F("deserializeJson() returned "));
            Serial.println(err.c_str());
            return;
        }
        sequencer->setChannelGain(t, trackDoc["output1Gain"] | 0.5, trackDoc["output2Gain"] | 0.5);
        JsonArray patterns = trackDoc["patterns"];

        //Serial.print("track");
        //Serial.println(t);
        p = 0;
        for (JsonObject pattern : patterns){
            //Serial.print("pattern");
            //Serial.println(p);
            SequencerPattern & sequencerPattern = sequencer->tracks[t].patterns[p];
            sequencerPattern.triggerState = pattern["triggerState"] | 0;
            sequencerPattern.pLockArmState = pattern["pLockArmState"] | 0;
            sequencerPattern.offset = pattern["offset"] | 0;
            sequencerPattern.trackLength = pattern["trackLength"] | 16;
            sequencerPattern.autoMutate = pattern["autoMutate"] | false;
            JsonArray steps = pattern["steps"];
            s = 0;
            for (JsonObject step : steps){
                //Serial.print("step ");
                //Serial.print(s);
                //Serial.print(":");
                SequencerStep & sequencerStep = sequencerPattern.steps[s];
                JsonArray stepParams = step["params"];
                sequencerStep.triggerMask = step["triggerMask"] | 0b00111111;
                ParameterSet & params = sequencerStep.params;
                params.parameter1 = stepParams[0];
                params.parameter2 = stepParams[1];
                params.parameter3 = stepParams[2];
                params.parameter4 = stepParams[3];
                params.parameter5 = stepParams[4];
                params.parameter6 = stepParams[5];
                s++;
            }
            p++;
        }
        t++;
    } while (file.findUntil(",","]"));

    // Close the file
    file.close();
    activeProject = 0 | _BV(projectNum);
    Serial.println(F("Finished load"));
};

boolean ProjectPersistence::exists(int projectNum){
        return existingProjects & _BV(projectNum);
};

boolean ProjectPersistence::isActive(int projectNum){
        return activeProject & _BV(projectNum);
};



