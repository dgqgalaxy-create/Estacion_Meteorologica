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

  public:
    // Constructor
    StatusLed(uint8_t _pin) {
      pin = _pin;
      pinMode(pin, OUTPUT);
      estadoLed = LOW;
      parpadeando = false;
      digitalWrite(pin, LOW);
    }

    // Encender fijo
    void encender() {
      parpadeando = false;
      estadoLed = HIGH;
      digitalWrite(pin, HIGH);
    }

    // Apagar fijo
    void apagar() {
      parpadeando = false;
      estadoLed = LOW;
      digitalWrite(pin, LOW);
    }

    // Configurar parpadeo (sin bloquear)
    void parpadear(unsigned long _intervalo) {
      // Solo reiniciar temporizador si cambia el modo o intervalo
      if (!parpadeando || intervalo != _intervalo) {
        parpadeando = true;
        intervalo = _intervalo;
        ultimoCambio = millis();
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
    }
};

#endif