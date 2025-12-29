#include <xc.h>                       
// Incluye los registros y definiciones del compilador XC8 específicos del PIC18F4550.
// Necesario para poder usar registros como TRISx, LATx, TMR0, etc.

#define _XTAL_FREQ 1000000       
// Define la frecuencia del oscilador del PIC (1 MHz).
// Es obligatorio para que las funciones __delay_ms() y __delay_us() generen el tiempo correcto.

#include "LibLCDXC8_3.h"         
// Incluye la librería propia para manejar el LCD.
// Aquí están las funciones: ConfiguraLCD, InicializaLCD, EscribeLCD_c, MensajeLCD_Var,
// DireccionaLCD, CrearCaracter, BorraLCD, DesplazaPantallaD, OcultarCursor, MostrarCursor, etc.

// ================= CONFIGURACIÓN DE BITS DE CONFIGURACIÓN =================

#pragma config FOSC=INTOSC_EC    
// Configura la fuente de reloj del PIC.
// INTOSC_EC: usa el oscilador interno del PIC y permite señal en OSC2 para otros fines.
// A nivel práctico aquí se esta usando el oscilador interno a 1 MHz (coherente con _XTAL_FREQ).

#pragma config WDT=OFF           
// Desactiva el Watchdog Timer (perro guardián).
// Evita que el PIC se reinicie solo si el programa se cuelga.

#pragma config LVP=OFF           
// Desactiva la programación en bajo voltaje.
// Libera el pin de LVP para uso normal y evita problemas si ese pin queda flotando.


                 //RGB - decenas 
                //RE2=Rojo
                //RE1=Azul
                //RE0=Verde
                //IMPORTANTE

//Las funciones se escriben despues del MAIN en C

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
// Este arreglo define 8 filas de 5 bits del carácter especial "estrella".
// Se usará en la función Bienvenida(), donde se llama CrearCaracter(Estrella, 0)
// para guardarla en la posición 0 de la CGRAM del LCD.

// Carácter propio: marco/cuadro para marcar entrada de datos
unsigned char Marco[8] = {
    0b11111,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b11111   // Marco de 5x8 lleno en el borde
};
// Este arreglo define un ?cuadro? para subrayar/encuadrar la posición donde el usuario digita.
// Se usará en PreguntaAlUsuario(), llamando a CrearCaracter(Marco, 1) para guardarlo
// en la posición 1 de CGRAM y luego se imprime con EscribeLCD_c(1).

// =========================== VARIABLES GLOBALES ===========================

// Contadores de piezas
unsigned int piezasTotalesContadas;   
// Lleva el total de piezas contadas desde que se inició o se reinició el sistema.
// Se incrementa en el while principal cuando se detecta un flanco de subida en RC1.
// Se compara contra piezasObjetivo para saber si ya se alcanzó la meta.

unsigned int unidades7Seg;           
// Representa las unidades (0?9) que se muestran en el display de 7 segmentos.
// Se incrementa cada vez que se cuenta una pieza (RC1 detectado).
// Cuando llega a 10, se reinicia a 0 y se incrementa decenasRGB.

unsigned int decenasRGB;             
// Contiene las decenas (0?5) que se representan con el LED RGB.
// Cada 10 piezas incrementa en 1. Cuando llega a 6, se reinicia a 0.
// Se usa para cambiar el color del RGB usando LATE en el while principal
// y también cuando se fuerza FIN.

// Entrada del objetivo por teclado
unsigned char indiceDigitoObjetivo;  
// Indica qué dígito del objetivo está ingresando el usuario:
// 0 ? primer dígito (decenas), 1 ? segundo dígito (unidades).
// Se usa en ConfigPregunta() y se reinicia en ConfigVariables() y PreguntaAlUsuario().

unsigned char modoEdicionObjetivo;   
// Bandera: 1 ? el usuario está escribiendo el objetivo en el LCD,
// 0 ? ya no se está editando.
// Se usa en PreguntaAlUsuario(), ConfigPregunta() y Borrar().

unsigned int piezasObjetivo;         
// Meta de piezas que se desean contar. Debe estar entre 1 y 59.
// Se construye a partir de las teclas del teclado matricial en ConfigPregunta()
// y se valida en PreguntaAlUsuario().

// Control del flujo de conteo
unsigned char flagConteoActivo;      
// 1 ? se está en el ciclo de conteo (while interno).
// 0 ? no se está contando (permite salir del while interno y volver a pedir objetivo).
// Se activa al inicio del conteo y se pone en 0 cuando se cumple la cuenta.

unsigned char teclaLeida;            
// Guarda la última tecla detectada en el teclado matricial.
// Se actualiza en la ISR (interrupción de PORTB) y se consulta en PreguntaAlUsuario()
// y en el while que espera la tecla OK ('*') después de cumplir la cuenta.

unsigned char pulsadorListo;         
// Bandera de antirrebote para el pulsador/sensor en RC1.
// 1 ? listo para detectar una nueva pulsación (espera bajada),
// 0 ? ya se detectó bajada y se está esperando la subida.
// Se usa en el bucle de conteo para evitar múltiples conteos por un mismo pulso.

// Inactividad
unsigned char segundosSinActividad;  
// Cuenta segundos (aprox) sin actividad de usuario.
// Se incrementa en la interrupción de Timer0.
// A los 10 s: se apaga la ?luz? (LATA3).
// A los 20 s: se ejecuta Sleep() y luego se reinicia al despertar.

// =========================== PROTOTIPOS DE FUNCIONES ===========================

void __interrupt() ISR(void);          
// Prototipo de la rutina de servicio de interrupciones.
// Atiende:
//  - Timer0 (parpadeo LED operación, inactividad, Sleep).
//  - Cambio en PORTB (teclado matricial).

void ConfigVariables(void);          
// Carga valores iniciales a TODAS las variables globales.
// Se llama al inicio del main y también después de cumplir la cuenta.

void Bienvenida(void);                
// Muestra la animación y mensaje de bienvenida en el LCD.
// Inicializa el LCD a 4 bits, crea el carácter Estrella y desplaza el texto.

void PreguntaAlUsuario(void);    
// Rutina que pregunta por el objetivo de piezas (01?59).
// Se queda en un while interno hasta que el usuario ingrese un valor válido y pulse 'OK'.

void ConfigPregunta(void);        
// Rutina que arma el número de dos dígitos del objetivo a partir de teclas.
// Se llama desde la ISR de PORTB cada vez que se presiona una tecla numérica 0?9
// mientras modoEdicionObjetivo = 1.

void Borrar(void);                      
// Borra el objetivo digitado por el usuario en el LCD y resetea piezasObjetivo.
// Se llama desde la ISR cuando se presiona la tecla SUPR (RB7 en fila 3).

// ================================ PROGRAMA PRINCIPAL ================================

void main (void){
    // 1. Inicializar variables globales
    ConfigVariables();
    // Se dejan todos los contadores y banderas en estado conocido (0 o inicial).
    // pulsadorListo = 1, unidades7Seg = 0, piezasTotalesContadas = 0, etc.

    modoEdicionObjetivo = 0;         
    // Al inicio no estamos editando el objetivo en el LCD.

    // 2. Configuración de puertos
    ADCON1 = 0b001111;               
    // Configura los pines analógicos como digitales en los puertos A, B y E
    // según la tabla de ADCON1 del 18F4550.
    // Aquí se pretende dejar RA0?RA4, RB0?RB4 y RE0?RE2 como digitales,
    // para usarlos con el LCD, RGB, teclado, etc.

    // --- LED RGB (decenas) en Puerto E: RE0, RE1, RE2 ---
    TRISE = 0;                       
    // Pone todos los pines de PORTE como salidas digitales: RE0, RE1, RE2.
    // Se usan para controlar el color del LED RGB según decenasRGB.

    LATE  = 0b00000111;              
    // Inicialmente pone 1 en RE0, RE1, RE2 (según el hardware, lógica inversa).
    // Se asume ?apagado?. Después se sobreescribe
    // con los colores en el bucle de conteo.

    // --- Siete segmentos (unidades) en Puerto D ---
    TRISD = 0;                       
    // Todos los pines de PORTD como salidas. Conectados a un decodificador BCD/7 segmentos
    // o directamente a segmentos.

    LATD  = unidades7Seg;            
    // Muestra en el puerto D el valor actual de unidades7Seg (al inicio será 0).
    // A medida que se incrementa unidades7Seg en el bucle de conteo, se actualiza LATD.

    // --- LED de operación en RA1 ---
    TRISA1 = 0;                      
    // Configura RA1 como salida digital. Se usa para el LED de ?operación? (parpadeo).
    LATA1  = 0;                      
    // LED apagado inicialmente. Luego parpadea en la ISR de Timer0.

    // --- Buzzer o segundo LED en RA2 ---
    TRISA2 = 0;                      
    // RA2 como salida digital. Se usa para un buzzer o LED de aviso cuando:
    // - se completa una decena (beep corto),
    // - se cumple la meta (beep largo).
    LATA2  = 0;                      

    // --- Pines para control del LCD (según la librería) ---
    TRISA3 = 0;                      
    // RA3 como salida
    // Aquí RA3 se usa en la ISR para apagar/encender led de operacion.
    LATA3  = 0;

    TRISA4 = 0;                      
    // RA4 como salida digital. En la librería, RS se mapea a LATA4.
    // Por tanto este pin es importante para distinguir comandos/datos hacia el LCD.

    // --- Pulsador / sensor de conteo en RC1 ---
    TRISC1 = 1;                      
    // RC1 como entrada digital. Aquí conectas el pulsador o sensor que detecta la pieza.
    // Se lee en el while de conteo para incrementar los contadores.

    // --- Backlight del LCD en RA5 ---
    TRISA5  = 0;                     
    // RA5 como salida digital. En tu hardware se usa para el backlight del LCD.
    LATA5   = 1;                     
    // Enciende el backlight al inicio.

    // ===================== CONFIGURACIÓN DE INTERRUPCIONES =====================

    // --- TIMER0: parpadeo del LED de operación ---
    T0CON  = 0b00000001;             
    // Configura Timer0:
    // bit7 TMR0ON = 0 (todavía apagado, aunque luego se pone en 1)
    // bit6 T08BIT = 0 ? modo 16 bits
    // bit5 T0CS   = 0 ? usa reloj interno (Fosc/4)
    // bit4 T0SE   = 0 ? flanco de subida (no aplica en reloj interno)
    // bit3 PSA    = 0 ? el prescaler está asignado a Timer0
    // bits2-0 T0PS = 001 ? prescaler 1:4
    // Con esto se calcula el tiempo de interrupción usando la precarga 3036.

    TMR0   = 3036;                   
    // Precarga inicial para que Timer0 desborde aproximadamente cada 1 segundo.
    // Cuando desborda, se activa TMR0IF y se atiende en la ISR.

    TMR0IF = 0;                      
    // Limpia la bandera de interrupción de Timer0 (por si estaba en 1 al arranque).

    TMR0IE = 1;                      
    // Habilita la interrupción local de Timer0 (periférico TMR0).

    TMR0ON = 1;                      
    // Enciende Timer0. A partir de aquí empieza a contar.

    // --- TECLADO MATRICIAL en PORTB ---
    TRISB = 0b11110000;              
    // Configura PORTB:
    // RB0?RB3 = 0 ? salidas (filas del teclado).
    // RB4?RB7 = 1 ? entradas (columnas del teclado).

    LATB  = 0b00000000;              
    // Deja inicialmente las filas en 0. Se irán activando una a una en la ISR.

    RBPU  = 0;                       
    // Habilita los pull-up internos en RB4?RB7 (cuando se configuran como entradas).
    // Esto asegura que las columnas estén en '1' cuando ninguna tecla está presionada.

    __delay_ms(100);                 
    // Pequeño retardo para que se estabilicen las entradas del teclado al inicio.

    RBIF  = 0;                       
    // Limpia la bandera de interrupción por cambio en PORTB.
    // Esta bandera se usa para detectar cuándo cambia alguna columna (se presiona una tecla).

    RBIE  = 1;                       
    // Habilita la interrupción por cambio en RB4?RB7 (teclado matricial).

    // --- Habilitación global de interrupciones ---
    PEIE = 1;                        
    // Habilita interrupciones de periféricos (Timer0, PORTB, etc.).

    GIE  = 1;                        
    // Habilitación global de interrupciones. A partir de aquí, las interrupciones están activas.

    // ============================= INICIO DEL PROGRAMA =============================

    Bienvenida();                    
    // Llama a la función que:
    //  - Configura el LCD a 4 bits (ConfiguraLCD(4), InicializaLCD()).
    //  - Crea el carácter Estrella en CGRAM (CrearCaracter(Estrella,0)).
    //  - Muestra el mensaje de bienvenida con estrellas alrededor.
    //  - Desplaza el texto con DesplazaPantallaD() para dar animación.

    while(1){
        // Bucle infinito principal del programa.

        // 1. Preguntar el número de piezas a contar (01 a 59)
        PreguntaAlUsuario();
        // Aquí se entra en un while interno que:
        //  - Dibuja el marco (CrearCaracter(Marco,1)).
        //  - Pide "Piezas a contar:".
        //  - Espera que el usuario digite dos dígitos (ConfigPregunta(), desde la ISR).
        //  - Valida que el número esté entre 1 y 59.
        //  - Sale solo cuando hay un objetivo válido y se presiona 'OK'.

        OcultarCursor();
        // Después de aceptar el objetivo, ya no queremos el cursor parpadeando en esa zona.

        // 2. Mostrar estado inicial en LCD: faltantes y objetivo
        MensajeLCD_Var("Faltantes: ");
        // Imprime la palabra "Faltantes: " en la primera línea.

        EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2);
        // Muestra cuántas piezas faltan para llegar al objetivo (2 dígitos).
        // Al inicio, piezasTotalesContadas = 0, así que muestra el objetivo completo.

        DireccionaLCD(0xC0);
        // Mueve el cursor al inicio de la segunda línea (dirección 0xC0).

        MensajeLCD_Var("Objetivo: ");
        // Imprime "Objetivo: " en la segunda línea.

        EscribeLCD_n8(piezasObjetivo, 2);
        // Escribe el número del objetivo (2 dígitos) a la derecha de "Objetivo:".

        flagConteoActivo = 1;        
        // Marca que estamos entrando al ciclo de conteo.
        // Esto habilita el while interno que maneja el conteo de piezas.

        // ========================= BUCLE PRINCIPAL DE CONTEO =========================
        while (flagConteoActivo == 1){

            // Caso: se llegó al objetivo de piezas
            if(piezasTotalesContadas == piezasObjetivo){

                // Aviso con RA2 (buzzer o LED) - señal de objetivo cumplido
                LATA2 = 1;
                __delay_ms(1000);
                LATA2 = 0;

                // Mensaje en pantalla de cuenta cumplida
                BorraLCD();
                MensajeLCD_Var("Cuenta Cumplida");
                DireccionaLCD(0xC4);
                MensajeLCD_Var("Presione OK");

                // Salir del ciclo de conteo
                flagConteoActivo = 0;
                teclaLeida       = '\0';
                // Se pone la bandera en 0 para romper el while interno,
                // y se limpia teclaLeida para esperar el 'OK'.

                // Esperar hasta que se pulse la tecla OK ('*')
                while(teclaLeida != '*'){}
                // Aquí no se lee el teclado en polling; la tecla se actualiza en la ISR de PORTB.
                // Cuando el usuario presione 'OK', en la interrupción se hará teclaLeida = '*'
                // y se romperá este while.

                // Volver a valores iniciales
                ConfigVariables();
                // Resetea todos los contadores, banderas y estado interno para iniciar de nuevo.
            }

            // Detección del pulsador/sensor en RC1 (activo en bajo)
            if(RC1 == 0 && piezasTotalesContadas != piezasObjetivo){
                // Si el pulsador está presionado (0 lógico) y todavía no hemos llegado al objetivo:
                segundosSinActividad = 0;  
                // Se reinicia el contador de inactividad para que no entre en Sleep.

                pulsadorListo        = 0;  
                // Se prepara para detectar el flanco de subida (cuando RC1 vuelva a 1).
                // Esto evita que cuente muchas veces mientras el botón está sostenido.
            }

            // Lógica de antirrebote: esperar a que RC1 vuelva a 1
            if(pulsadorListo == 0){
                // Solo entra aquí si ya detectamos una bajada (RC1 == 0 antes).

                if(RC1 == 1){
                    // Cuando vuelve a 1, interpretamos que hubo un pulso válido de conteo.

                    pulsadorListo = 1;     
                    // Se bloquea hasta que haya otra nueva bajada en RC1.

                    // Actualizar contadores
                    unidades7Seg++;        
                    // Aumenta las unidades (para el display de 7 segmentos).

                    piezasTotalesContadas++; 
                    // Aumenta el total de piezas.

                    // Cuando se llega a 10 unidades, se suma una decena
                    if (unidades7Seg == 10){

                        // Aviso corto con RA2: beep para indicar que se completó una decena
                        LATA2 = 1;
                        __delay_ms(300);
                        LATA2 = 0;

                        unidades7Seg = 0;  
                        // Reinicia unidades de 0 a 9

                        decenasRGB++;      
                        // Incrementa el contador de decenas, que luego se refleja en el color del RGB.

                        if (decenasRGB == 6){ 
                            // Si llega a 6 decenas (60 piezas), se reinicia a 0.
                            // Esto porque solo se permiten objetivos hasta 59.
                            decenasRGB = 0;
                        }
                    }

                    // Actualización de color del LED RGB según las decenas
                    if(decenasRGB == 0){
                        LATE = 0b00000001; // Magenta (Rojo+Azul) según tu conexión.
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
                    // Posiciona el cursor justo donde se imprimen los ?faltantes?
                    // dentro de la primera línea.

                    EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2);
                    // Escribe de nuevo cuántas piezas faltan (2 dígitos).

                    // Actualizar siete segmentos (unidades)
                    LATD = unidades7Seg;
                    // Refresca el valor en el display de 7 segmentos con las unidades actuales.

                    __delay_ms(500);       
                    // Retardo adicional para antirrebote y para que el conteo no sea demasiado rápido.
                }
            }
        }

        // Al salir del ciclo (cuando flagConteoActivo pasa a 0), fijar estado del RGB y siete segmentos
        LATE = 0b00000001;                 
        // Deja el RGB en Magenta como estado de ?reposo?.

        LATD = unidades7Seg;               
        // Muestra el último valor contado en el 7 segmentos (por si se vuelve a mostrar).
    }
}

// ======================= RUTINA DE SERVICIO DE INTERRUPCIÓN =======================

void __interrupt() ISR(void){
    // Esta función atiende todas las interrupciones habilitadas:
    //  - TMR0 (parpadeo, inactividad y Sleep).
    //  - PORTB (teclado matricial).

    // -------------------- INTERRUPCIÓN POR TIMER0 (LED OPERACIÓN) --------------------
    if(TMR0IF == 1){
        // Entra aquí cuando Timer0 desborda (overflow).

        TMR0   = 3036;                    
        // Recarga el valor inicial para volver a generar otro periodo de ~1 segundo.

        TMR0IF = 0;                       
        // Limpia la bandera para poder detectar la próxima interrupción.

        LATA1  = LATA1 ^ 1;               
        // Conmuta el estado del LED en RA1 usando XOR:
        //  - Si estaba en 0, pasa a 1.
        //  - Si estaba en 1, pasa a 0.
        // Resultado: LED de operación parpadea cada segundo aprox.

        segundosSinActividad++;           
        // Cada vez que se ejecuta esta ISR (1 vez por segundo), incrementa el contador
        // de segundos sin actividad. Esta variable se reinicia a 0 cuando:
        //  - Hay pulsos en RC1 (conteo de piezas).
        //  - Hay tecleo en el teclado (se reasigna en ISR de PORTB).

        // Apagar backlight a los 10 segundos de inactividad
        if(segundosSinActividad == 10){
            // Backlight = 0 (en comentario originalmente).
            LATA3 = 0;                    
            // Aquí RA3 se está usando para apagar algo (por ejemplo, la luz del LCD).
            // Depende de tu hardware cómo está conectado.
        }

        // Entrar en suspensión a los 20 segundos de inactividad
        if(segundosSinActividad >= 20){
            Sleep();                      
            // Instrucción especial del PIC: entra en modo bajo consumo.
            // El PIC se detiene hasta que haya una interrupción que lo despierte
            // (por ejemplo, un cambio en PORTB o interrupción externa).

            // ---- DESPUÉS DE DESPERTAR LLEGA AQUÍ ----
            segundosSinActividad = 0;     
            // Al despertar, se reinicia el contador de inactividad.

            RBIF = 0;                     
            // Se limpia la bandera de interrupción de PORTB (por si esa fue la causa del despertar).

            TMR0ON = 1;                   
            // Se asegura que Timer0 vuelva a estar encendido después de Sleep.
        }
    }

    // -------------------- INTERRUPCIÓN POR CAMBIO EN PORTB (TECLADO) ----------------
    if(RBIF == 1){
        // Entra aquí cuando hay un cambio en RB4?RB7 (teclado matricial).

        if(PORTB != 0b11110000){          
            // Se verifica que realmente haya una tecla presionada.
            // Si todo está en '1' (1111 en columnas), no hay tecla.

            segundosSinActividad = 0;     
            // Hubo actividad del usuario ? se reinicia inactividad.

            LATB = 0b11111110;            
            // Se activa la fila 1 (RB0 = 0) y se dejan RB1?RB3 en 1.
            // Así se puede leer qué columna (RB4?RB7) está en 0 para detectar la tecla.

            // -------- FILA 1 (RB0 activa) --------
            if(RB4 == 0){                 
                // Columna RB4 en 0 con fila 1 activa ? tecla '1'.
                teclaLeida = 1;
                ConfigPregunta();
                // Llama a ConfigPregunta() para construir el número del objetivo
                // si estamos en modoEdicionObjetivo.
            }            
            else if(RB5 == 0){            
                // Tecla '2'
                teclaLeida = 2; 
                ConfigPregunta();
            }
            else if(RB6 == 0){            
                // Tecla '3'
                teclaLeida = 3; 
                ConfigPregunta();
            }
            else if(RB7 == 0){            
                // Tecla OK ('*')
                teclaLeida = '*'; 
                // No llama a ConfigPregunta() porque esta tecla confirma la entrada.
                // Se usa en:
                //  - PreguntaAlUsuario(): para salir del while que espera '*'.
                //  - Después de "Cuenta Cumplida": para continuar.
            }

            // -------- FILA 2 (RB1 activa) --------
            else{
                LATB = 0b11111101;        
                // Activa fila 2 (RB1 = 0).

                if(RB4 == 0){             
                    // Tecla '4'
                    teclaLeida = 4;
                    ConfigPregunta();
                }
                else if(RB5 == 0){        
                    // Tecla '5'
                    teclaLeida = 5; 
                    ConfigPregunta();
                }
                else if(RB6 == 0){        
                    // Tecla '6'
                    teclaLeida = 6; 
                    ConfigPregunta();
                }
                else if(RB7 == 0){        
                    // Tecla de PARADA DE EMERGENCIA.
                    // Usada para detener totalmente el sistema.

                    LATE = 0b00000011;    
                    // Pone el LED RGB en rojo (según tu tabla de colores).

                    BorraLCD(); 
                    OcultarCursor();
                    MensajeLCD_Var("    PARADA DE");
                    DireccionaLCD(0xC2);
                    MensajeLCD_Var("EMERGENCIA");

                    while(1){}            
                    // Bucle infinito ? el sistema queda "muerto" hasta reset.
                }

                // -------- FILA 3 (RB2 activa) --------
                else{
                    LATB = 0b11111011;    
                    // Activa fila 3 (RB2 = 0).

                    if(RB4 == 0){         
                        // Tecla '7'
                        teclaLeida = 7; 
                        ConfigPregunta();
                    }
                    else if(RB5 == 0){    
                        // Tecla '8'
                        teclaLeida = 8; 
                        ConfigPregunta();
                    }
                    else if(RB6 == 0){    
                        // Tecla '9'
                        teclaLeida = 9; 
                        ConfigPregunta();
                    } 
                    else if(RB7 == 0){    
                        // Tecla SUPR (borrar objetivo).
                        Borrar();
                        // Limpia lo que el usuario estaba escribiendo como objetivo.
                    }

                    // -------- FILA 4 (RB3 activa) --------
                    else{
                        LATB = 0b11110111; 
                        // Activa fila 4 (RB3 = 0).

                        if(RB4 == 0){      
                            // Tecla de REINICIO de conteo.
                            unidades7Seg          = 0;
                            piezasTotalesContadas = 0;
                            decenasRGB            = 0;

                            LATE = 0b00000001; 
                            // LED RGB vuelve a Magenta.

                            if(flagConteoActivo == 1){
                                // Si estamos en modo conteo, actualizamos también el LCD y el 7 segmentos.

                                DireccionaLCD(0x8B);
                                // Nos paramos donde se muestran los faltantes.

                                EscribeLCD_n8(piezasObjetivo - piezasTotalesContadas, 2);
                                // Muestra de nuevo los faltantes (que ahora es el objetivo completo).

                                LATD = unidades7Seg;
                                // Siete segmentos vuelve a 0.
                            }
                        }
                        else if(RB5 == 0){ 
                            // Tecla '0'
                            teclaLeida = 0; 
                            ConfigPregunta();
                        }
                        else if(RB6 == 0){ 
                            // Tecla FIN: fuerza que la cuenta se considere como cumplida
                            // sin tener que contar físicamente todas las piezas.

                            Borrar();
                            piezasTotalesContadas = piezasObjetivo;
                            // Se iguala el conteo total al objetivo.

                            decenasRGB = piezasObjetivo / 10;
                            // Se calculan las decenas del objetivo para el RGB.

                            unidades7Seg = piezasObjetivo - decenasRGB * 10;
                            // Se calculan las unidades para el 7 segmentos.

                            // Actualizar color RGB según decenas
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

                            LATD = unidades7Seg; 
                            // Muestra en el 7 segmentos las unidades calculadas del objetivo.
                        }
                        else if(RB7 == 0){ 
                            // Tecla LUZ: control manual del backlight o luz asociada a RA3.
                            LATA3 = LATA3 ^ 1;  
                            // Conmuta RA3 (enciende/apaga la luz).

                            TMR0ON = 1;         
                            // Asegura que Timer0 siga encendido después de esta acción.
                        }
                    } 
                }
            }

            LATB = 0b11110000;          
            // Restablece las filas: RB0?RB3 = 1, se ?desactiva? el teclado
            // hasta la próxima interrupción.

        }

        __delay_ms(300);                
        // Retardo para antirrebote general del teclado. Evita que una misma pulsación
        // genere múltiples interrupciones demasiado seguidas.

        RBIF = 0;                       
        // Limpia la bandera de interrupción de PORTB para poder detectar nuevos cambios.
    }
}

// ======================== FUNCIÓN: CONFIGURAR VARIABLES ========================

void ConfigVariables(void){ 
    // Esta función deja todas las variables globales en estado inicial.
    // Se llama:
    //  - al comienzo del main,
    //  - después de cumplir la cuenta y pulsar OK.

    pulsadorListo         = 1;   
    // Indica que el pulsador RC1 está listo para detectar una nueva pulsación.
    // Con esto evitamos contar hasta que veamos primero una bajada a 0.

    unidades7Seg          = 0;   
    // El display de 7 segmentos arranca mostrando 0.

    flagConteoActivo      = 0;   
    // Al inicio no estamos dentro del ciclo de conteo.

    piezasTotalesContadas = 0;  
    // Ninguna pieza ha sido contada todavía.

    decenasRGB            = 0;   
    // Las decenas de RGB arrancan en 0 ? el color inicial se pondrá como magenta
    // cuando se actualice.

    indiceDigitoObjetivo  = 0;   
    // Inicialmente se está en el primer dígito del objetivo.

    piezasObjetivo        = 0;   
    // Objetivo inicial inválido (0). Se cambia cuando el usuario ingresa un valor.

    teclaLeida            = '\0';
    // Sin tecla válida leída todavía.

    segundosSinActividad  = 0;   
    // Contador de inactividad en 0. Empezará a contar a partir del Timer0.
}

// ======================== FUNCIÓN: MENSAJE DE BIENVENIDA ========================

void Bienvenida(void){
    // CONFIGURACIÓN DEL LCD

    ConfiguraLCD(4);          
    // Indica a la librería que se trabajará en modo de interfaz de 4 bits.

    InicializaLCD();          
    // Ejecuta la secuencia de inicialización del LCD (comandos especiales),
    // borra la pantalla y enciende el display.

    OcultarCursor();
    // Oculta el cursor para que no se vea el parpadeo durante la bienvenida.

    // Crear carácter especial Estrella en la posición 0 de CGRAM
    CrearCaracter(Estrella, 0);
    // Envía el arreglo Estrella a la CGRAM del LCD en la posición 0.
    // A partir de aquí, EscribeLCD_c(0) dibuja la estrella.

    // Mensaje en pantalla con estrellas decorativas
    EscribeLCD_c(0); 
    EscribeLCD_c(0);
    MensajeLCD_Var(" Bienvenido ");
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    // Imprime dos estrellas, la palabra " Bienvenido " y otras dos estrellas en la primera línea.

    DireccionaLCD(0xC0);
    // Mueve el cursor al inicio de la segunda línea.

    EscribeLCD_c(0);
    EscribeLCD_c(0);
    MensajeLCD_Var("  Operario ");
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    // Escribe "  Operario " en la segunda línea, también rodeado de estrellas.

    __delay_ms(3200);
    // Pausa ~3.2 segundos para que el operador pueda leer el mensaje.

    // Desplazar el texto hacia la derecha (pequeña animación)
    for(int i = 0; i < 18; i++){
        DesplazaPantallaD();
        // Cada llamada manda el comando de desplazar la pantalla un carácter a la derecha.
        __delay_ms(100);
        // Pequeño retardo para ver la animación suavemente.
    }
}

// ======================== FUNCIÓN: PREGUNTAR AL USUARIO ========================

void PreguntaAlUsuario(void){ 
    // Se encarga de todo el flujo de preguntar ?Piezas a contar?.
    // No sale de esta función hasta que el usuario ingrese un objetivo válido
    // (entre 01 y 59) y pulse la tecla OK ('*').

    while(1){
        CrearCaracter(Marco, 1);       
        // Crea el carácter especial Marco en la posición 1 de CGRAM.
        // Es un cuadro para marcar la posición de ingreso.

        indiceDigitoObjetivo = 0;           
        // Arrancamos escribiendo el primer dígito (decenas).

        // Mensaje en pantalla para ingreso de objetivo
        BorraLCD();
        MensajeLCD_Var("Piezas a contar:");
        // Escribe en la primera línea el mensaje de solicitud.

        DireccionaLCD(0xC7);               
        // Posiciona el cursor en la segunda línea, columna adecuada para los dos dígitos.

        EscribeLCD_c(1);
        EscribeLCD_c(1);
        // Escribe dos veces el carácter especial 'Marco' (posición 1 en CGRAM),
        // marcando visualmente donde irán los dos dígitos.

        DireccionaLCD(0xC7);
        // Vuelve a posicionar el cursor al inicio del primer dígito.

        MostrarCursor();
        // Muestra el cursor para indicar que el usuario puede escribir.

        modoEdicionObjetivo = 1;           
        // Activa el modo de edición. Esto hace que ConfigPregunta() sea efectiva.

        teclaLeida          = '\0';
        // Limpia la tecla previa.

        // Esperar a que se presione OK ('*')
        while(teclaLeida != '*'){
            // Este while queda ?esperando? a que la ISR de PORTB detecte una tecla
            // y actualice teclaLeida. ConfigPregunta() se encarga de imprimir
            // los dígitos y armar piezasObjetivo.
        }

        // Validar el rango del objetivo: 01?59
        if((piezasObjetivo > 59) || (piezasObjetivo == 0)){
            // Si el valor ingresado está fuera de rango:
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
            // Después del mensaje de error, el while(1) se repite
            // y vuelve a pedir "Piezas a contar:".
        }else{
            // Valor aceptado (entre 1 y 59)
            modoEdicionObjetivo  = 0;
            indiceDigitoObjetivo = 0;
            BorraLCD();
            teclaLeida           = '\0';
            break;                         
            // Sale del while(1) y por tanto de PreguntaAlUsuario().
        }
    }
}

// ======================== FUNCIÓN: CONFIGURAR ENTRADA DE OBJETIVO ========================

void ConfigPregunta(void){ 
    // Función que se llama desde la ISR de PORTB cada vez que se pulsa una tecla numérica.
    // Construye el valor de piezasObjetivo en base a dos dígitos: decenas y unidades,
    // siempre que modoEdicionObjetivo = 1.

    if(indiceDigitoObjetivo == 0 && modoEdicionObjetivo == 1){
        // Primer dígito: decenas del objetivo.

        EscribeLCD_n8(teclaLeida, 1);      
        // Escribe el dígito (0?9) en el LCD como un solo carácter.

        piezasObjetivo = teclaLeida;       
        // Guarda ese dígito como parte inicial del objetivo.
        // De momento vale [0?9], luego se completará con el segundo dígito.
    }
    else if(indiceDigitoObjetivo == 1 && modoEdicionObjetivo == 1){
        // Segundo dígito: unidades del objetivo.

        EscribeLCD_n8(teclaLeida, 1);      
        // Escribe el segundo dígito en el LCD.

        piezasObjetivo = piezasObjetivo * 10 + teclaLeida; 
        // Forma el número de dos cifras:
        // Ejemplo: si el primer dígito fue 3 y el segundo 5:
        // piezasObjetivo = 3 * 10 + 5 = 35.

        OcultarCursor();
        // Una vez que se completan los dos dígitos, se oculta el cursor.
        // A partir de este momento se espera solo a que el usuario pulse '*'.
    }

    indiceDigitoObjetivo++;                
    // Avanza al siguiente dígito. Después de 2, ya no se usan más dígitos.
}

// ======================== FUNCIÓN: BORRAR OBJETIVO DEL USUARIO ========================

void Borrar(void){ 
    // Borra el valor escrito por el usuario en el LCD y reinicia la entrada del objetivo.
    // Se llama desde la ISR de PORTB cuando se presiona la tecla SUPR.

    if(modoEdicionObjetivo == 1){
        // Solo tiene sentido borrar si estamos en modo de edición.

        MostrarCursor();
        // Muestra el cursor para señalar que puede volver a escribir.

        piezasObjetivo          = 0;
        indiceDigitoObjetivo = 0;
        // Reinicia el objetivo y el índice de dígito.

        DireccionaLCD(0xC7);
        // Se posiciona donde estaban los cuadros de Marco.

        EscribeLCD_c(1);
        EscribeLCD_c(1);
        // Vuelve a dibujar dos caracteres Marco (posición 1 en CGRAM),
        // indicando que el usuario puede volver a digitar dos cifras.

        DireccionaLCD(0xC7);
        // Vuelve a ubicar el cursor en la posición del primer dígito.
    }
}