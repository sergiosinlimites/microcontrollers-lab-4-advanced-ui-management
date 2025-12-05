#include <xc.h>
#define _XTAL_FREQ 1000000       // Frecuencia del oscilador del PIC (1 MHz) para que __delay_ms/us funcione bien
#include "LibLCDXC8.h"         // Librería propia para manejo del LCD

#pragma config FOSC=INTOSC_EC    // Usar oscilador interno
#pragma config WDT=OFF           // Desactivar perro guardián (Watchdog Timer)
#pragma config LVP=OFF           // Desactivar programación en bajo voltaje (LVP)

// =========================== CARACTERES ESPECIALES LCD ===========================

// Carácter propio: estrella (se guarda en CGRAM del LCD)
unsigned char Estrella[8] = {
    0b00100,   
    0b01110,  //  *** 
    0b11111,  // *****
    0b01110,  //  *** 
    0b11111,  // *****
    0b01110,  //  *** 
    0b00100,  //   *  
    0b00000   // vacío
};

// Carácter propio: raya al piso para marcar posición de entrada de datos
unsigned char Marco[8] = {
    0b11111,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b11111   // ______
};

// =========================== VARIABLES GLOBALES ===========================

// Contadores de piezas
unsigned int piezasTotalesContadas;   // Conteo global de piezas (todas las piezas contadas hasta ahora)
unsigned int unidades7Seg;           // Unidades (0–9) que se muestran en el siete segmentos
unsigned int decenasRGB;             // Decenas (0–5) que se muestran con el led RGB

// Entrada del objetivo por teclado
unsigned char indiceDigitoObjetivo;  // 0: primer dígito, 1: segundo dígito del valor objetivo
unsigned char modoEdicionObjetivo;   // 1: usuario puede escribir en el LCD, 0: no puede
unsigned int piezasObjetivo;         // Meta de piezas a contar (01–59)

// Control del flujo de conteo
unsigned char flagConteoActivo;      // 1: estamos en el ciclo de conteo, 0: no
unsigned char teclaLeida;            // Última tecla detectada en el teclado
unsigned char pulsadorListo;         // Antirrebote del pulsador RC1 (1: listo para nueva cuenta, 0: ya contó)

// Inactividad
unsigned char segundosSinActividad;  // Cuenta en segundos el tiempo sin actividad (Timer1)

// =========================== PROTOTIPOS DE FUNCIONES ===========================

void __interrupt() ISR(void);        // Rutina de servicio de interrupciones
void ConfigVariables(void);          // Carga valores iniciales a las variables globales
void Bienvenida(void);               // Mensaje de bienvenida en el LCD
void PreguntaAlUsuario(void);        // Rutina para preguntar y leer el objetivo de piezas
void ConfigPregunta(void);           // Manejo de cada dígito del objetivo
void Borrar(void);                   // Borra el objetivo digitado por el usuario

// ================================ PROGRAMA PRINCIPAL ================================

void main (void){
    // 1. Inicializar variables globales
    ConfigVariables();
    modoEdicionObjetivo = 0;         // Al inicio no estamos en modo edición en LCD

    // 2. Configuración de puertos
    ADCON1 = 0b00001111;               // Configura RA0-RA4, RB0-RB4 y RE0-RE2 como digitales (no analógicos)

    // --- LED RGB (decenas) en Puerto E: RE0, RE1, RE2 ---
    TRISE = 0;                       // Todos los pines de PORTE como salidas
    LATE  = 0b00000111;              // Lógica inicial (depende del hardware, aquí se asume apagado)

    // --- Siete segmentos (unidades) en Puerto D ---
    TRISD = 0;                       // Todos los pines de PORTD como salidas
    LATD  = unidades7Seg;            // Mostrar el valor actual de unidades7Seg

    // --- LED de operación en RA1 ---
    TRISA1 = 0;                      // RA1 como salida digital
    LATA1  = 0;                      // LED apagado inicialmente

    // --- Buzzer o segundo LED en RA2 ---
    TRISA2 = 0;                      // RA2 como salida digital
    LATA2  = 0;                      // Apagado inicialmente

    // --- Pines para control del LCD (según tu librería) ---
    TRISA3 = 0;                      // RS del LCD
    LATA3  = 0;
    TRISA4 = 0;                      // E del LCD

    // --- Pulsador / sensor de conteo en RC1 ---
    TRISC1 = 1;                      // RC1 como entrada digital

    // --- Backlight del LCD en RA5 ---
    TRISA5 = 0;                      // RA5 como salida digital
    LATA5  = 1;                      // Backlight encendido inicialmente

    // ===================== CONFIGURACIÓN DE INTERRUPCIONES =====================

    // --- TIMER0: parpadeo del LED de operación ---
    T0CON  = 0b00000001;             // Timer0 modo 16 bits, prescaler 1:4
    TMR0   = 3036;                   // Precarga para generar ~1 s entre interrupciones
    TMR0IF = 0;                      // Limpiar bandera de interrupción de Timer0
    TMR0IE = 1;                      // Habilitar interrupción de Timer0
    TMR0ON = 1;                      // Encender Timer0

    // --- TECLADO MATRICIAL en PORTB ---
    TRISB = 0b11110000;              // RB0-RB3: salidas (filas); RB4-RB7: entradas (columnas)
    LATB  = 0b00000000;              // Inicialmente filas en 0
    RBPU  = 0;                       // Habilitar resistencias de pull-up en RB4-RB7
    __delay_ms(100);                 // Tiempo para que se estabilicen las entradas
    RBIF  = 0;                       // Limpiar bandera de interrupción por cambio en PORTB
    RBIE  = 1;                       // Habilitar interrupción por teclado (PORTB)

    // --- Habilitación global de interrupciones ---
    PEIE = 1;                        // Habilitar interrupciones de periféricos
    GIE  = 1;                        // Habilitación global de interrupciones

    // ============================= INICIO DEL PROGRAMA =============================

    Bienvenida();                    // Muestra mensaje inicial en el LCD con la estrella

    while(1){
        // 1. Preguntar el número de piezas a contar (01 a 59)
        PreguntaAlUsuario();
        OcultarCursor();

        // 2. Mostrar estado inicial en LCD: faltantes y objetivo
        MensajeLCD_Var("Faltantes: ");
        EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2);
        DireccionaLCD(0xC0);
        MensajeLCD_Var("Objetivo: ");
        EscribeLCD_n8(piezasObjetivo, 2);

        flagConteoActivo = 1;        // Entramos al ciclo de conteo

        // ========================= BUCLE PRINCIPAL DE CONTEO =========================
        while (flagConteoActivo == 1){

            // Caso: se llegó al objetivo de piezas
            if(piezasTotalesContadas == piezasObjetivo){

                // Aviso con RA2 (buzzer o LED)
                LATA2 = 1;
                __delay_ms(1000);
                LATA2 = 0;

                // Mensaje en pantalla
                BorraLCD();
                MensajeLCD_Var("Cuenta Cumplida");
                DireccionaLCD(0xC4);
                MensajeLCD_Var("Presione OK");

                // Salir del ciclo de conteo
                flagConteoActivo = 0;
                teclaLeida       = '\0';

                // Esperar hasta que se pulse la tecla OK ('*')
                while(teclaLeida != '*'){}

                // Volver a valores iniciales
                ConfigVariables();
            }

            // Detección del pulsador/sensor en RC1 (activo en bajo)
            if(RC1 == 0 && piezasTotalesContadas != piezasObjetivo){
                segundosSinActividad = 0;  // Si se está contando, no quiero entrar en sleep
                pulsadorListo        = 0;  // Habilitar detección de flanco de subida
            }

            // Lógica de antirrebote: esperar a que RC1 vuelva a 1
            if(pulsadorListo == 0){
                if(RC1 == 1){
                    pulsadorListo = 1;     // Bloquear hasta la próxima bajada

                    // Actualizar contadores
                    unidades7Seg++;        // Unidades para el siete segmentos
                    piezasTotalesContadas++; // Conteo global de piezas

                    // Cuando se llega a 10 unidades, se suma una decena
                    if (unidades7Seg == 10){

                        // Aviso corto con RA2
                        LATA2 = 1;
                        __delay_ms(300);
                        LATA2 = 0;

                        unidades7Seg = 0;  // Reiniciar unidades
                        decenasRGB++;      // Incrementar decenas (para el LED RGB)

                        if (decenasRGB == 6){ // Hasta 59 piezas (0–5 decenas)
                            decenasRGB = 0;
                        }
                    }

                    // Actualización de color del LED RGB según las decenas
                    if(decenasRGB == 0){
                        LATE = 0b00000001; // Magenta
                    }else if(decenasRGB == 1){
                        LATE = 0b00000101; // Azul
                    }else if(decenasRGB == 2){
                        LATE = 0b00000100; // Cyan
                    }else if(decenasRGB == 3){
                        LATE = 0b00000110; // Verde
                    }else if(decenasRGB == 4){
                        LATE = 0b00000010; // Amarillo
                    }else if(decenasRGB == 5){
                        LATE = 0b00000000; // Blanco
                    }

                    // Actualizar faltantes en el LCD
                    DireccionaLCD(0x8B);
                    EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2);

                    // Actualizar siete segmentos (unidades)
                    LATD = unidades7Seg;

                    __delay_ms(500);       // Delay extra para el antirrebote del pulsador
                }
            }
        }

        // Al salir del ciclo, fijar estado del RGB y siete segmentos
        LATE = 0b00000001;                 // RGB en Magenta
        LATD = unidades7Seg;               // Mostrar el último valor contado
    }
}

// ======================= RUTINA DE SERVICIO DE INTERRUPCIÓN =======================

void __interrupt() ISR(void){

    // -------------------- INTERRUPCIÓN POR TIMER0 (LED OPERACIÓN) --------------------
    if(TMR0IF == 1){
        TMR0   = 3036;                    // Recargar Timer0 para ~1 s
        TMR0IF = 0;                       // Limpiar bandera
        LATA1  = LATA1 ^ 1;               // Conmutar LED de operación en RA1

        segundosSinActividad++;           // Aumentar contador de segundos sin actividad

        // Apagar backlight a los 10 segundos de inactividad
        if(segundosSinActividad == 10){
            //Backlight = 0;
            LATA3 = 0;
        }

        // Entrar en suspensión a los 20 segundos de inactividad
        if(segundosSinActividad >= 20){
            Sleep();                      // Suspender PIC (modo bajo consumo)

            // ---- DESPUÉS DE DESPERTAR LLEGA AQUÍ ----
            segundosSinActividad = 0;     // Reiniciar contador de inactividad
            RBIF = 0;                     // Limpiar interrupción por teclado
            TMR0ON = 1;                   // Volver a encender Timer1
        }
    }

    // -------------------- INTERRUPCIÓN POR TIMER1 (INACTIVIDAD) ---------------------
    if (TMR1IF){
        TMR1   = 34286;                   // Recargar Timer1
        TMR1IF = 0;                       // Limpiar bandera

        segundosSinActividad++;           // Aumentar contador de segundos sin actividad

        // Apagar backlight a los 10 segundos de inactividad
        if(segundosSinActividad == 10){
            //Backlight = 0;
            LATA3 = 0;
        }

        // Entrar en suspensión a los 20 segundos de inactividad
        if(segundosSinActividad >= 20){
            Sleep();                      // Suspender PIC (modo bajo consumo)

            // ---- DESPUÉS DE DESPERTAR LLEGA AQUÍ ----
            segundosSinActividad = 0;     // Reiniciar contador de inactividad
            RBIF = 0;                     // Limpiar interrupción por teclado
            TMR1ON = 1;                   // Volver a encender Timer1
        }
    }

    // -------------------- INTERRUPCIÓN POR CAMBIO EN PORTB (TECLADO) ----------------
    if(RBIF == 1){
        if(PORTB != 0b11110000){          // Confirmar que realmente hubo cambio en columnas (RB4-RB7)

            segundosSinActividad = 0;     // Hubo actividad -> resetear inactividad
            LATB = 0b11111110;            // Activar fila 1 (RB0=0, RB1–RB3=1)

            // -------- FILA 1 (RB0 activa) --------
            if(RB4 == 0){                 // Tecla '1'
                teclaLeida = 1;
                ConfigPregunta();
            }            
            else if(RB5 == 0){            // Tecla '2'
                teclaLeida = 2; 
                ConfigPregunta();
            }
            else if(RB6 == 0){            // Tecla '3'
                teclaLeida = 3; 
                ConfigPregunta();
            }
            else if(RB7 == 0){            // Tecla OK ('*')
                teclaLeida = '*'; 
            }

            // -------- FILA 2 (RB1 activa) --------
            else{
                LATB = 0b11111101;        // RB1=0, resto de filas=1

                if(RB4 == 0){             // Tecla '4'
                    teclaLeida = 4;
                    ConfigPregunta();
                }
                else if(RB5 == 0){        // Tecla '5'
                    teclaLeida = 5; 
                    ConfigPregunta();
                }
                else if(RB6 == 0){        // Tecla '6'
                    teclaLeida = 6; 
                    ConfigPregunta();
                }
                else if(RB7 == 0){        // PARADA DE EMERGENCIA
                    LATE = 0b00000011;    // Poner LED RGB en rojo (según combinación de pines)
                    BorraLCD(); 
                    OcultarCursor();
                    MensajeLCD_Var("    PARADA DE");
                    DireccionaLCD(0xC2);
                    MensajeLCD_Var("EMERGENCIA");

                    while(1){}            // Bucle infinito (detiene el sistema)
                }

                // -------- FILA 3 (RB2 activa) --------
                else{
                    LATB = 0b11111011;    // RB2=0

                    if(RB4 == 0){         // Tecla '7'
                        teclaLeida = 7; 
                        ConfigPregunta();
                    }
                    else if(RB5 == 0){    // Tecla '8'
                        teclaLeida = 8; 
                        ConfigPregunta();
                    }
                    else if(RB6 == 0){    // Tecla '9'
                        teclaLeida = 9; 
                        ConfigPregunta();
                    } 
                    else if(RB7 == 0){    // Tecla SUPR (borrar objetivo)
                        Borrar();
                    }

                    // -------- FILA 4 (RB3 activa) --------
                    else{
                        LATB = 0b11110111; // RB3=0

                        if(RB4 == 0){      // REINICIO de conteo
                            unidades7Seg         = 0;
                            piezasTotalesContadas = 0;
                            decenasRGB           = 0;
                            LATE                 = 0b00000001; // RGB magenta

                            if(flagConteoActivo == 1){
                                DireccionaLCD(0x8B);
                                EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2);
                                LATD = unidades7Seg;
                            }
                        }
                        else if(RB5 == 0){ // Tecla '0'
                            teclaLeida = 0; 
                            ConfigPregunta();
                        }
                        else if(RB6 == 0){ // Tecla FIN (forzar objetivo cumplido)
                            Borrar();
                            piezasTotalesContadas = piezasObjetivo;
                            decenasRGB            = piezasObjetivo / 10;
                            unidades7Seg          = piezasObjetivo - decenasRGB * 10;

                            // Actualizar color RGB según decenas
                            if(decenasRGB == 0){
                                LATE = 0b00000010; 
                            }else if(decenasRGB == 1){
                                LATE = 0b00000011; 
                            }else if(decenasRGB == 2){
                                LATE = 0b00000001; 
                            }else if(decenasRGB == 3){
                                LATE = 0b00000101; 
                            }else if(decenasRGB == 4){
                                LATE = 0b00000100; 
                            }else if(decenasRGB == 5){
                                LATE = 0b00000000; 
                            }

                            LATD = unidades7Seg; // Mostrar unidades en siete segmentos
                        }
                        else if(RB7 == 0){ // Tecla LUZ (backlight manual)
                            LATA3 = LATA3 ^ 1;  // Conmutar RA3 (según hardware, se usa para luz)
                            TMR0ON = 1;         // Asegurar que Timer1 esté encendido
                        }
                    }
                }
            }

            LATB = 0b11110000;          // Restablecer configuración por defecto de filas (todas inactivas)
        }

        __delay_ms(300);                // Antirrebote general del teclado
        RBIF = 0;                       // Limpiar bandera de interrupción de PORTB
    }
}

// ======================== FUNCIÓN: CONFIGURAR VARIABLES ========================

void ConfigVariables(void){ //Valores iniciales de las variables
    pulsadorListo        = 1;   // No hay pulsación pendiente en RC1
    unidades7Seg         = 0;   // Siete segmentos inicia en 0
    flagConteoActivo     = 0;   // No estamos en el ciclo de conteo
    piezasTotalesContadas = 0;  // Ninguna pieza contada al inicio
    decenasRGB           = 0;   // Decenas en 0 (RGB en estado inicial)
    indiceDigitoObjetivo = 0;   // Primer dígito del objetivo
    piezasObjetivo       = 0;   // Objetivo inicialmente 0 (inválido)
    teclaLeida           = '\0';// Sin tecla válida leída
    segundosSinActividad = 0;   // Contador de inactividad en 0
}

// ======================== FUNCIÓN: MENSAJE DE BIENVENIDA ========================

void Bienvenida(void){
    // CONFIGURACIÓN DEL LCD
    ConfiguraLCD(4);          // Modo de 4 bits
    InicializaLCD();          // Inicialización del controlador del LCD
    OcultarCursor();

    // Crear carácter especial Estrella en la posición 0 de CGRAM
    CrearCaracter(Estrella, 0);

    // Mensaje en pantalla con estrellas decorativas
    EscribeLCD_c(0); //Llama a latecla especial
    EscribeLCD_c(0);
    MensajeLCD_Var(" Bienvenido ");
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    
    DireccionaLCD(0xC0);
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    MensajeLCD_Var("  Operario ");
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    __delay_ms(3200);

    // Desplazar el texto hacia la derecha (pequeña animación)
    for(int i = 0; i < 18; i++){
        DesplazaPantallaD();
        __delay_ms(100);
    }
}

// ======================== FUNCIÓN: PREGUNTAR AL USUARIO ========================

void PreguntaAlUsuario(void){ // Encargada del setup de la pregunta de elementos a contar
    while(1){
        CrearCaracter(Marco, 1);       // Crear carácter de raya al piso en posición 1
        indiceDigitoObjetivo = 0;           // Vamos a escribir desde el primer dígito

        // Mensaje en pantalla para ingreso de objetivo
        BorraLCD();
        MensajeLCD_Var("Piezas a contar:");
        DireccionaLCD(0xC7);               // Posicionar cursor donde se escriben los dígitos
        EscribeLCD_c(1);
        EscribeLCD_c(1);
        DireccionaLCD(0xC7);
        MostrarCursor();

        modoEdicionObjetivo = 1;           // Habilitar edición por parte del usuario
        teclaLeida          = '\0';

        // Esperar a que se presione OK ('*')
        while(teclaLeida != '*'){
            // La lectura de teclas se maneja por interrupciones (RBIF)
        }

        // Validar el rango del objetivo: 01–59
        if((piezasObjetivo > 59) || (piezasObjetivo == 0)){
            modoEdicionObjetivo = 0;
            teclaLeida          = '\0';
            piezasObjetivo      = 0;

            // Mensaje de error por rango inválido
            BorraLCD();
            OcultarCursor();
            MensajeLCD_Var("     !Error!");
            __delay_ms(1000);
            BorraLCD();
            MensajeLCD_Var("Valor max: 59");
            DireccionaLCD(0xC0);
            MensajeLCD_Var("Valor min: 01");
            __delay_ms(2000);
            BorraLCD();
        }else{
            // Valor aceptado
            modoEdicionObjetivo = 0;
            indiceDigitoObjetivo = 0;
            BorraLCD();
            teclaLeida = '\0';
            break;                         // Salir de la función si el objetivo es válido
        }
    }
}

// ======================== FUNCIÓN: CONFIGURAR ENTRADA DE OBJETIVO ========================

void ConfigPregunta(void){ // Función para el ingreso del valor a contar (dos dígitos)
    if(indiceDigitoObjetivo == 0 && modoEdicionObjetivo == 1){
        // Primer dígito (decenas)
        EscribeLCD_n8(teclaLeida, 1);      // Mostrar dígito en pantalla
        piezasObjetivo = teclaLeida;       // Guardar primer dígito
    }else if(indiceDigitoObjetivo == 1 && modoEdicionObjetivo == 1){
        // Segundo dígito (unidades)
        EscribeLCD_n8(teclaLeida, 1);      // Mostrar dígito en pantalla
        piezasObjetivo = piezasObjetivo * 10 + teclaLeida; // Formar número de dos cifras
        OcultarCursor();
    }
    indiceDigitoObjetivo++;                // Pasar a siguiente posición
}

// ======================== FUNCIÓN: BORRAR OBJETIVO DEL USUARIO ========================

void Borrar(void){ // Borrar el valor escrito por el usuario
    if(modoEdicionObjetivo == 1){
        MostrarCursor();
        piezasObjetivo      = 0;
        indiceDigitoObjetivo = 0;
        DireccionaLCD(0xC7);
        EscribeLCD_c(1);
        EscribeLCD_c(1);
        DireccionaLCD(0xC7);
    }
}
