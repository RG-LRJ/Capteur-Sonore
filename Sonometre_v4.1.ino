// Programme de mesure de niveau de bruit
// Romain Garel
// Spécifique Matter (Utilisation d'un capteur de pression pour les mesures car le capteur sonore n'existe pas encore dans Matter)

#include <Matter.h>
#include <MatterPressure.h>
#include <EEPROM.h>

#define MODE_TEST 1
#define MODE_NOMINAL 2
#define MODE MODE_TEST   // MODE_NOMINAL ou MODE_TEST (pour tester les capteurs sans Matter en suivant les mesures via le terminal)

#define ON LOW      // Gestion de la led RGB
#define OFF HIGH

// Type de capteur micro ("ELECTRET" ou "MEMS")
#define ELECTRET 1
#define MEMS 2
#define MICRO ELECTRET      // "ELECTRET" ou "MEMS"

#define HISTORY_SIZE 10     // Nombre de mesures pour le calcul de la moyenne de bruit pour la calibration
#define TOP_VALUES_COUNT 50 // Nombre de valeurs max à conserver																 

// Instance du capteur Matter
MatterPressure matter_pressure_sensor;

// Constantes (utilisez constexpr pour une meilleure optimisation)
constexpr uint16_t REFERENCE_LEVEL_LOW = 200;           // Niveau cible en silence (base)
constexpr uint16_t REFERENCE_LEVEL_HIGH = 800;          // Niveau cible avec bruit (haut)
constexpr uint16_t EEPROM_MAGIC_NUMBER = 0xCAFE;
constexpr uint8_t EEPROM_ADDR_MAGIC = 0;
constexpr uint8_t EEPROM_ADDR_OFFSET = 2;
constexpr uint8_t EEPROM_ADDR_GAIN = 6;
constexpr uint8_t CALIBRATION_BUTTON = BTN_BUILTIN;     // Pin du bouton de calibration (avec pull-up interne)
constexpr uint8_t SOUND_SENSOR = A0;                    // Micro branché sur l'entrée A0
constexpr uint16_t LOOPTIME = 1000;                     // Durée de la boucle de mesure (1s)
constexpr uint16_t SAMPLE_DELAY = 1;                    // Délai minimal entre échantillons (ms)

constexpr float MATTER_SCALE_FACTOR_ELECTRET = 1.0f;   // Facteur de conversion pour Matter (car capteur de pression)
constexpr float MATTER_SCALE_FACTOR_MEMS = 20.0f;      // Facteur de conversion pour Matter (car capteur de pression)

// Variables globales 
float SENSOR_OFFSET = 0.0f;                 // Offset pour la valeur de base (silence)
float SENSOR_GAIN = 1.0f;                   // Facteur de gain pour la sensibilité
bool calibrationRequested = false;          // Flag pour demande de calibration
uint8_t calibrationStep = 0;                // Étape de calibration (0=non demandée, 1=silence, 2=bruit)
struct SoundMeasurement {
    uint16_t current;      					// Mesure actuelle
    uint16_t average;      					// Moyenne des mesures
    uint16_t minimum;      					// Valeur minimale
    uint16_t maximum;      					// Valeur maximale
    uint16_t peak;         					// ecart de valeur min et max
    uint16_t noise;        					// ecart de valeur moyenne et max
    uint16_t noiseTop10;   					// moyenne des 10 plus grandes valeurs																 
    uint32_t sum;          					// Somme des mesures (uint32_t pour éviter overflow)
    uint16_t count;        					// Nombre de mesures
    uint32_t startTime;    					// Temps de début de mesure
	uint16_t topValues[TOP_VALUES_COUNT];  	// Tableau pour les 10 plus grandes valeurs																				  
};

SoundMeasurement measurement = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}};

// historique des mesures de bruit
struct {
    uint16_t history[HISTORY_SIZE];
    uint8_t index;
    uint8_t count;      // Nombre de valeurs (pour gérer le démarrage)
    float average;    	// moyenne de l'hitorique des valeurs
} noise;

// Variables pour limiter la fréquence d'envoi Matter
uint32_t lastMatterUpdate = 0;
constexpr uint32_t MATTER_UPDATE_INTERVAL = 5000;   // Mise à jour toutes les 5 secondes
uint16_t lastSentValue = 0;
constexpr uint16_t MATTER_THRESHOLD_ELECTRET = 2;   // Seuil de changement minimal ELECTRET pour envoi
constexpr uint16_t MATTER_THRESHOLD_MEMS = 1;       // Seuil de changement minimal MEMS pour envoi
uint16_t MATTER_THRESHOLD = 0;                      // Seuil de changement minimal pour envoi

void setup() {
    Serial.begin(115200);
    
    // Configuration de l'entrée analogique pour la lecture
    pinMode(SOUND_SENSOR, INPUT);
    pinMode(CALIBRATION_BUTTON, INPUT_PULLUP);  // Bouton avec pull-up interne

    // Led de connexion Matter | bleu: initialisation - vert: connecté - rouge: non connecté
    pinMode(LEDR, OUTPUT);
    digitalWrite(LEDR, ON);
    pinMode(LEDG, OUTPUT);
    digitalWrite(LEDG, OFF);
    pinMode(LEDB, OUTPUT);
    digitalWrite(LEDB, OFF);

    // GESTION DE LA CALIBRATION
    if (loadCalibrationFromEEPROM()) {
        Serial.println("\nCalibration chargée depuis l'EEPROM");
        Serial.print("Offset: ");
        Serial.print(SENSOR_OFFSET);
        Serial.print(" | Gain: ");
        Serial.println(SENSOR_GAIN);
        Serial.println("Appuyez sur le bouton pour recalibrer si nécessaire\n");
    } else {
        Serial.println("\nPremier démarrage - Calibration requise");
        SENSOR_OFFSET = 0.0f;
        SENSOR_GAIN = 1.0f;
        saveCalibrationToEEPROM();
    }
    
    // Initialisation Matter
    if (MODE == MODE_NOMINAL) {
        initializeMatter();
    }
    
    Serial.println("Capteur de bruit initialisé et prêt!");
}

void initializeMatter() {
    Matter.begin();
    matter_pressure_sensor.begin();
    
    Serial.println("Capteur de pression Matter");
    
    // Gestion de l'appairage
    if (!Matter.isDeviceCommissioned()) {
        digitalWrite(LEDR, OFF);
        Serial.println("Capteur non appairé");
        Serial.println("Appairer le capteur à l'aide du Code Manuel ou du QR Code");
        Serial.printf("Code d'appairage manuel: %s\n", Matter.getManualPairingCode().c_str());
        Serial.printf("URL du QR Code: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
        
        // La led clognote en bleu le temps de l'appairage
        while (!Matter.isDeviceCommissioned()) {
            delay(500);
            digitalWrite(LEDB, ON);
            delay(500);
            digitalWrite(LEDB, OFF);
        }
    }
    
    Serial.println("En attente du réseau Thread...");
    digitalWrite(LEDR, OFF);
    // La led reste en bleu jusqu'à la connexion au réseau Thread
    digitalWrite(LEDB, ON);
    while (!Matter.isDeviceThreadConnected()) {
        delay(200);
    }
    Serial.println("Connecté au réseau Thread!");
    digitalWrite(LEDB, OFF);
    
    Serial.println("En attente du service de gestion des équipements Matter...");
    // La led clognote en vert jusqu'à la connexion Matter
    while (!matter_pressure_sensor.is_online()) {
        delay(500);
        digitalWrite(LEDG, ON);
        delay(500);
        digitalWrite(LEDG, OFF);
    }
    Serial.println("Capteur de pression connecté et opérationnel!");
    // La led reste verte
    digitalWrite(LEDG, ON);
}

void loop() {
    // Réinitialisation des variables de mesure
    measurement.maximum = 0;
    measurement.minimum = 4095;
    measurement.peak = 0;
    measurement.noise = 0;
	measurement.noiseTop10 = 0;						   
    measurement.sum = 0;
    measurement.count = 0;
    measurement.startTime = millis();
    
	// Reinitialisation du tableau des top valeurs à 0
    for (uint8_t i = 0; i < TOP_VALUES_COUNT; i++) {
        measurement.topValues[i] = 0;
    }

    // Vérifier si le bouton de calibration est pressé
    checkCalibrationButton();

    // Effectuer la calibration si demandée
    if (calibrationRequested) {
        performAutoCalibration();
        saveCalibrationToEEPROM();
        calibrationRequested = false;
    }

    // Boucle de mesure optimisée
    performMeasurements();
    analyseMeasurements();
    
    // Affichage des résultats
    displayResults();
    
    // Envoi des données via Matter (avec limitation de fréquence)
    updateMatterSensor();
    
    // Petit délai pour éviter une surcharge du processeur
    delay(10);
    digitalWrite(LEDB, OFF);
}

// Fonction pour insérer une valeur dans le tableau des valeurs max
void insertTopValue(uint16_t value) {
    // Trouver la position d'insertion (la plus petite valeur du top 10)
    uint8_t minIdx = 0;
    uint16_t minVal = measurement.topValues[0];
    
    for (uint8_t i = 1; i < TOP_VALUES_COUNT; i++) {
        if (measurement.topValues[i] < minVal) {
            minVal = measurement.topValues[i];
            minIdx = i;
        }
    }
    
    // Si la nouvelle valeur est plus grande que la plus petite du top 10, on la remplace
    if (value > minVal) {
        measurement.topValues[minIdx] = value;
    }
}
void performMeasurements() {
    uint32_t currentTime;
    uint32_t lastSampleTime = measurement.startTime;
    
    while ((currentTime = millis()) - measurement.startTime < LOOPTIME) {
        // Vérification de débordement de millis()
        if (currentTime < measurement.startTime) {
            // Gestion du rollover de millis() 
            measurement.startTime = currentTime;
            break;
        }
        
        // Limitation de la fréquence d'échantillonnage
        if (currentTime - lastSampleTime >= SAMPLE_DELAY) {
            // mesure
            measurement.current = analogRead(SOUND_SENSOR);
            
            // Mise à jour du minimum
            if (measurement.current < measurement.minimum) {
                measurement.minimum = measurement.current;
            }
            
            // Mise à jour du maximum
            if (measurement.current > measurement.maximum) {
                measurement.maximum = measurement.current;
            }
            // mise à jour du top 10 des valeurs max
            insertTopValue(measurement.current);
			
            // Accumulation avec protection contre overflow
            uint32_t newSum = measurement.sum + measurement.current;
            if (newSum >= measurement.sum) { // Vérification overflow
                measurement.sum = newSum;
                measurement.count++;
            } else {
                Serial.println("Attention: Overflow détecté dans la somme!");
                break;
            }
            
            lastSampleTime = currentTime;
        }
        
        // Yield pour permettre aux autres tâches de s'exécuter
        yield();
    }
}

void analyseMeasurements() {
    // Calcul des moyenne, peak et noise (avec protection contre division par zéro)
    if (measurement.count > 0) {
        measurement.average = measurement.sum / measurement.count;
        measurement.peak = measurement.maximum - measurement.minimum;
        measurement.noise = measurement.maximum - measurement.average;
        
        // Calcul de la moyenne des 10 plus grandes valeurs
        uint32_t sumTop10 = 0;
        for (uint8_t i = 0; i < TOP_VALUES_COUNT; i++) {
            sumTop10 += measurement.topValues[i];
        }
        uint16_t avgTop10 = sumTop10 / TOP_VALUES_COUNT;
        uint16_t rawNoise = avgTop10 - measurement.average;
        
        if (!calibrationRequested) {
            // Application de la calibration à deux points: (valeur * gain) + offset
            measurement.noiseTop10 = (rawNoise * SENSOR_GAIN) + SENSOR_OFFSET;
        } else{
            // pas de correction si calibration en cours
            measurement.noiseTop10 = rawNoise;
        }
    	
    } else {
        measurement.average = 0;
		measurement.noiseTop10 = 0;						   
        Serial.println("Attention: Aucune mesure effectuée!");
    }

    // Gestion de l'hitorique des valeurs
    noise.index = (noise.index + 1) % HISTORY_SIZE;
    //noise.history[noise.index] = measurement.noise;
    noise.history[noise.index] = measurement.noiseTop10;
    if (noise.count < HISTORY_SIZE) {
        noise.count++;  // Incrémente jusqu'à HISTORY_SIZE
    }
    // Calcul de la moyenne avec le nombre réel de valeurs
    if (noise.count != 0) {
        uint32_t sum = 0;
        for (int i = 0; i < noise.count; i++) {
            sum += noise.history[i];
        }
        noise.average = sum / noise.count;
    }
}

// Pour affichage des mesure sur le terminal du PC
void displayResults() {
    //Serial.print("Nb mesures: ");
    //Serial.print(measurement.count);
    Serial.print(" Moyenne: ");
    Serial.print(measurement.average);
    Serial.print(" | Min: ");
    Serial.print(measurement.minimum);
    Serial.print(" | Max: ");
    Serial.print(measurement.maximum);
    Serial.print(" | Peak: ");
    Serial.print(measurement.peak);
    Serial.print(" | Bruit: ");
    Serial.print(measurement.noise);
    Serial.print(" | Bruit Top10: ");
    Serial.print(measurement.noiseTop10); 
    Serial.print(" | Bruit moyen: ");
    Serial.println(noise.average);
    //Serial.print(" | Durée réelle: ");
    //Serial.print(millis() - measurement.startTime);
    //Serial.println(" ms");
}

void performAutoCalibration() {
    static float calibLowValue = 0.0f;  // Stockage de la mesure en silence
    
    if (calibrationStep == 1) {
        // ÉTAPE 1: Calibration en SILENCE
        Serial.println("\n=== CALIBRATION ÉTAPE 1/2 ===");
        Serial.println("SILENCE: Veuillez maintenir le silence ambiant...");
        Serial.println("Calibration dans 3 secondes...");

        // Compte à rebours avec clignotement LED
        digitalWrite(LEDR, OFF);
        digitalWrite(LEDG, OFF);
        digitalWrite(LEDB, OFF);
        for (int i = 5; i > 0; i--) {
            Serial.println(i);
            digitalWrite(LEDB, ON);
            delay(500);
            digitalWrite(LEDB, OFF);
            delay(500);
        }
        digitalWrite(LEDR, ON);

        // Boucle de mesure pour calibration
        for (int i = HISTORY_SIZE; i > 0; i--) {
            measurement.maximum = 0;
            measurement.minimum = 4095;
            measurement.peak = 0;
            measurement.noise = 0;
            measurement.noiseTop10 = 0;
            measurement.sum = 0;
            measurement.count = 0;
            measurement.startTime = millis();
            
            for (uint8_t i = 0; i < TOP_VALUES_COUNT; i++) {
                measurement.topValues[i] = 0;
            }
            performMeasurements();
            analyseMeasurements();
        }
        
        calibLowValue = noise.average;
        
        Serial.println("=== RÉSULTATS ÉTAPE 1 ===");
        Serial.print("Valeur en silence: ");
        Serial.println(calibLowValue);
        Serial.println("\n>>> ÉTAPE 2: Veuillez générer un BRUIT MODÉRÉ constant <<<");
        Serial.println("(Parlez normalement ou faites du bruit pendant 10 secondes)");
        Serial.println("Appuyez à nouveau sur le bouton quand vous êtes prêt...\n");
        
        // Signal visuel (bleu rapide)
        digitalWrite(LEDR, OFF);
        digitalWrite(LEDG, OFF);
        digitalWrite(LEDB, OFF);
        for (int i = 0; i < 5; i++) {
            digitalWrite(LEDB, ON);
            delay(200);
            digitalWrite(LEDB, OFF);
            delay(200);
        }
        digitalWrite(LEDB, ON);
        
        // calibrationStep = 0;  // Attente du prochain appui
        calibrationRequested = false;
        
    } else if (calibrationStep == 2) {
        // ÉTAPE 2: Calibration avec BRUIT
        Serial.println("\n=== CALIBRATION ÉTAPE 2/2 ===");
        Serial.println("BRUIT: Maintenez un bruit modéré constant...");
        Serial.println("Calibration dans 3 secondes...");

        // Compte à rebours
        digitalWrite(LEDR, OFF);
        digitalWrite(LEDG, OFF);
        digitalWrite(LEDB, OFF);
        for (int i = 5; i > 0; i--) {
            Serial.println(i);
            digitalWrite(LEDB, ON);
            delay(500);
            digitalWrite(LEDB, OFF);
            delay(500);
        }
        digitalWrite(LEDR, ON);

        // Boucle de mesure
        for (int i = HISTORY_SIZE; i > 0; i--) {
            measurement.maximum = 0;
            measurement.minimum = 4095;
            measurement.peak = 0;
            measurement.noise = 0;
            measurement.noiseTop10 = 0;
            measurement.sum = 0;
            measurement.count = 0;
            measurement.startTime = millis();
            
            for (uint8_t i = 0; i < TOP_VALUES_COUNT; i++) {
                measurement.topValues[i] = 0;
            }
            performMeasurements();
            analyseMeasurements();
        }
        
        float calibHighValue = noise.average;
        
        // Calcul des paramètres de calibration
        // Formule: valeur_calibrée = (valeur_brute * GAIN) + OFFSET
        // Système à 2 équations:
        // REFERENCE_LEVEL_LOW = (calibLowValue * GAIN) + OFFSET
        // REFERENCE_LEVEL_HIGH = (calibHighValue * GAIN) + OFFSET
        
        float measureRange = calibHighValue - calibLowValue;
        float referenceRange = REFERENCE_LEVEL_HIGH - REFERENCE_LEVEL_LOW;
        
        if (measureRange > 50) {  // Vérification que la différence est significative
            SENSOR_GAIN = referenceRange / measureRange;
            SENSOR_OFFSET = REFERENCE_LEVEL_LOW - (calibLowValue * SENSOR_GAIN);
        } else {
            Serial.println("ERREUR: Différence insuffisante entre silence et bruit!");
            Serial.println("Veuillez recommencer avec plus de contraste.");
            SENSOR_GAIN = 1.0f;
            SENSOR_OFFSET = 0.0f;
            calibrationStep = 0;
            calibrationRequested = false;
            return;
        }

        Serial.println("=== RÉSULTATS FINAUX ===");
        Serial.print("Silence mesuré: ");
        Serial.println(calibLowValue);
        Serial.print("Bruit mesuré: ");
        Serial.println(calibHighValue);
        Serial.print("Plage mesurée: ");
        Serial.println(measureRange);
        Serial.println("---");
        Serial.print("Offset calculé: ");
        Serial.println(SENSOR_OFFSET);
        Serial.print("Gain calculé: ");
        Serial.println(SENSOR_GAIN);
        Serial.println("---");
        Serial.print("Valeur silence calibrée: ");
        Serial.println((calibLowValue * SENSOR_GAIN) + SENSOR_OFFSET);
        Serial.print("Valeur bruit calibrée: ");
        Serial.println((calibHighValue * SENSOR_GAIN) + SENSOR_OFFSET);
        Serial.println("============================\n");

        // Signal visuel de réussite (vert)
        digitalWrite(LEDR, OFF);
        digitalWrite(LEDG, OFF);
        digitalWrite(LEDB, OFF);
        for (int i = 0; i < 5; i++) {
            digitalWrite(LEDG, ON);
            delay(200);
            digitalWrite(LEDG, OFF);
            delay(200);
        }
        digitalWrite(LEDG, ON);
        
        // Sauvegarde de la calibration
        saveCalibrationToEEPROM();
        calibrationStep = 0;
        calibrationRequested = false;
    }
}

void checkCalibrationButton() {
    static bool lastButtonState = HIGH;
    static uint32_t buttonPressTime = 0;
    static bool longPressDetected = false;

    bool currentButtonState = digitalRead(CALIBRATION_BUTTON);

    // Détection d'un appui (front descendant)
    if (lastButtonState == HIGH && currentButtonState == LOW) {
        buttonPressTime = millis();
        longPressDetected = false;
    }

    // Bouton maintenu enfoncé
    if (currentButtonState == LOW) {
        uint32_t pressDuration = millis() - buttonPressTime;

        // Appui long détecté (3 secondes)
        if (pressDuration >= 3000 && !longPressDetected) {
            longPressDetected = true;
            
            if (calibrationStep == 0) {
                // Démarrage de la calibration
                Serial.println("\n>>> CALIBRATION DEMANDÉE - ÉTAPE 1/2 <<<");
                calibrationStep = 1;
            } else if (calibrationStep == 1) {
                // Passage à l'étape 2
                Serial.println("\n>>> CALIBRATION ÉTAPE 2/2 <<<");
                calibrationStep = 2;
            }
            
            calibrationRequested = true;

            // Feedback visuel (LED bleue rapide)
            digitalWrite(LEDR, OFF);
            digitalWrite(LEDG, OFF);
            digitalWrite(LEDB, OFF);
            for (int i = 0; i < 10; i++) {
                digitalWrite(LEDB, ON);
                delay(100);
                digitalWrite(LEDB, OFF);
                delay(100);
                digitalWrite(LEDR, ON);
                delay(100);
                digitalWrite(LEDR, OFF);
                delay(100);
            }
            digitalWrite(LEDG, ON);
        }
    }

    lastButtonState = currentButtonState;
}

bool loadCalibrationFromEEPROM() {
    // Lire le magic number
    uint16_t magicNumber;
    EEPROM.get(EEPROM_ADDR_MAGIC, magicNumber);

    // Vérifier si l'EEPROM contient des données valides
    if (magicNumber == EEPROM_MAGIC_NUMBER) {
        // Lire l'offset et le gain
        EEPROM.get(EEPROM_ADDR_OFFSET, SENSOR_OFFSET);
        EEPROM.get(EEPROM_ADDR_GAIN, SENSOR_GAIN);

        // Vérifier que les valeurs sont dans une plage raisonnable
        if (SENSOR_OFFSET >= -1000 && SENSOR_OFFSET <= 1000 &&
            SENSOR_GAIN >= 0.1 && SENSOR_GAIN <= 10.0) {
            return true;
        } else {
            Serial.println("Paramètres EEPROM hors limites - réinitialisation");
            return false;
        }
    }

    return false;
}

void saveCalibrationToEEPROM() {
    // Sauvegarder le magic number
    EEPROM.put(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_NUMBER);

    // Sauvegarder l'offset et le gain
    EEPROM.put(EEPROM_ADDR_OFFSET, SENSOR_OFFSET);
    EEPROM.put(EEPROM_ADDR_GAIN, SENSOR_GAIN);

    Serial.println("Calibration sauvegardée dans l'EEPROM");
}

void updateMatterSensor() {
    uint32_t currentTime = millis();
    float matterValue = 0;
    
    // Vérifier si suffisamment de temps s'est écoulé depuis la dernière mise à jour
    bool timeToUpdate = (currentTime - lastMatterUpdate >= MATTER_UPDATE_INTERVAL);

    // calcul valeur à envoyer
    if (MICRO == ELECTRET) {
        // valeur moyenne car instable dans les mesures
        matterValue = (float)measurement.noiseTop10 * MATTER_SCALE_FACTOR_ELECTRET;
        MATTER_THRESHOLD = MATTER_THRESHOLD_ELECTRET;
    } else if (MICRO == MEMS) {
        // valeur max car plus stable qu'un micro Electret
        matterValue = (float)(measurement.noiseTop10) * MATTER_SCALE_FACTOR_MEMS;
        MATTER_THRESHOLD = MATTER_THRESHOLD_MEMS;
    }

    // Vérifier si la valeur a suffisamment changé
    uint16_t valueDiff = abs((int)matterValue - (int)lastSentValue);
    bool significantChange = (valueDiff >= MATTER_THRESHOLD);
    
    // Envoie de la mesure seulement si nécessaire
    if (timeToUpdate || significantChange) {
        Serial.print("Matter update: ");
        Serial.println(matterValue);
        // Vérification que le capteur est toujours en ligne
        if (matter_pressure_sensor.is_online()) {
            matter_pressure_sensor.set_measured_value(matterValue);
            digitalWrite(LEDB, ON);
            lastMatterUpdate = currentTime;
            lastSentValue = matterValue;
        } else {
            Serial.println("Capteur Matter hors ligne!");
            digitalWrite(LEDG, OFF);
            digitalWrite(LEDR, ON);
            // Initialisation Matter
            if (MODE == MODE_NOMINAL) {
                initializeMatter();
            }
        }
    }
}