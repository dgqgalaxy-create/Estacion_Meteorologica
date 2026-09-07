#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>

class StatusLed {
  private:
    uint8_t pin;
    bool estadoLed;
    unsigned long ultimoCambio;
    unsigned long intervalo;
    bool parpadeando;
    bool pulsando;
    unsigned long duracionEncendido;

  public:
    // Constructor
    StatusLed(uint8_t _pin) {
      pin = _pin;
      pinMode(pin, OUTPUT);
      estadoLed = LOW;
      parpadeando = false;
      pulsando = false;
      digitalWrite(pin, LOW);
    }

    // Encender fijo
    void encender() {
      parpadeando = false;
      pulsando = false;
      estadoLed = HIGH;
      digitalWrite(pin, HIGH);
    }

    // Apagar fijo
    void apagar() {
      parpadeando = false;
      pulsando = false;
      estadoLed = LOW;
      digitalWrite(pin, LOW);
    }

    // Configurar parpadeo (sin bloquear)
    void parpadear(unsigned long _intervalo) {
      // Solo reiniciar temporizador si cambia el modo o intervalo
      if (!parpadeando || intervalo != _intervalo) {
        pulsando = false;
        parpadeando = true;
        intervalo = _intervalo;
        ultimoCambio = millis();
      }
    }

    // Configurar pulso corto y pausa larga, sin bloquear
    void pulsar(unsigned long _pausa, unsigned long _duracionEncendido) {
      if (!pulsando || intervalo != _pausa || duracionEncendido != _duracionEncendido) {
        pulsando = true;
        parpadeando = false;
        intervalo = _pausa;
        duracionEncendido = _duracionEncendido;
        ultimoCambio = millis();
        estadoLed = HIGH;
        digitalWrite(pin, HIGH);
      }
    }

    // Esta función debe llamarse en el loop() siempre
    void actualizar() {
      if (parpadeando) {
        unsigned long tiempoActual = millis();
        if (tiempoActual - ultimoCambio >= intervalo) {
          ultimoCambio = tiempoActual;
          estadoLed = !estadoLed; // Invertir estado
          digitalWrite(pin, estadoLed);
        }
      }
      if (pulsando) {
        unsigned long tiempoActual = millis();
        unsigned long espera = estadoLed ? duracionEncendido : intervalo;
        if (tiempoActual - ultimoCambio >= espera) {
          ultimoCambio = tiempoActual;
          estadoLed = !estadoLed;
          digitalWrite(pin, estadoLed);
        }
      }
    }
};

#endif