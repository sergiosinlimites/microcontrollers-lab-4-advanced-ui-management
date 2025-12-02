#include <xc.h>
#define _XTAL_FREQ 1000000 //Definir la constante para el cálculo de retardos   
#include "LibLCDXC8.h" //Incluir libreria propia
#pragma config FOSC=INTOSC_EC //Configurar el reloj interno
#pragma config WDT=OFF //Desactivar el perro guardian
#pragma config LVP=OFF //Programar el PIC

//DECLARACIÓN DE VARIABLES 
unsigned char Corazon[8] = {
    0b01010,  //  * * 
    0b11111,  // *****
    0b11111,  // *****
    0b11111,  // *****
    0b01110,  //  *** 
    0b00100,  //   *  
    0b00000,  //      
    0b00000
};

unsigned char RayaAlPiso[]={
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b11111}; // Carácter nuevo

unsigned int Supercontador; //Contador global de las piezas
unsigned int contador; //Contador unidades (siete segmentos)
unsigned int contadorRGB; //Contador decenas (led rgb)
unsigned char posicion; // Posicion del número ingresado por el usuario (unidades o decenas)
unsigned char editor; // Variable para habilitar la escritura en el lcd del usuario 
unsigned int Objetivo; // Valor meta de las piezas a contar 
unsigned char salir; // Variable para salir del ciclo de conteo
unsigned char Tecla; // Tecla presionada en el teclado
unsigned char Pulsado; //Variable para evitar conteo infinito
unsigned char Inactividad; //Variable para inactividad

//DECLARACIÓN DE FUNCIONES 
void __interrupt()ISR(void);
void ConfigVariables(void);
void Bienvenida(void);
void PreguntaAlUsuario(void);
void ConfigPregunta(void);
void Borrar(void);


void main (void){
    //CONFIGURACION DE LOS VALORES INICIALES DE LAS VARIABLES
    ConfigVariables();
    editor=0;
    // CONFIGURACIÓN DE LOS PUERTOS
    ADCON1=0b001111; //Quital las funciones analogas de los pines RA0-RA4, RB0-RB4 , RE0-RE2  

    // Pines para el RGB
    TRISE=0; // Todos los pines del puerto E son salidas digitales
    LATE=0b00000111; // Todos los pines de salida del puerto E en 0

    // Pines para el Siete segmentos
    TRISD=0; // Todos los pines del puerto E son salidas digitales
    LATD=contador; // El puerto es igual a el valor del contador 

    // Pin del led de operación
    TRISA1=0; // Pin A1 es configurado como salida digital
    LATA1=0; // La salida del Pin A1 es 0

    TRISA2=0; // Pin A2 es configurado como salida digital para el led de operacion
    LATA2=0; // La salida del Pin A2 es 0

    // Pines para el uso de la lcd
    TRISA3=0; //RS
    LATA3=0;
    TRISA4=0; //E

    //Pin del pulsador de conteo
    TRISC1=1; //Pin C1 es configurado como entrada digital

    // Pin prender apagar backlight LCD
    TRISA5=0; // Pin A1 es configurado como salida digital
    LATA5=1; // La salida del Pin A5 es 0 (backlight prendido)

    // CONFIGURACIÓN DE LAS INTERRUPCIONES //

    // Configuración de la interrupción del TIMER0
    T0CON=0b00000001; //Configuración del timer0 modo 16 bits - prescale 4
    TMR0=3036; // Valor de precarga
    TMR0IF=0; // Bandera inicializada en 0
    TMR0IE=1; // Habilitación local de la interrupción 
    TMR0ON=1; // Encender el Timer0

    // Configuración de la interrupción del TIMER1  
    T1CON = 0b10110001;               // RD16, 1:8, interno, ON [web:16]
    TMR1=34286;
    TMR1IF=0; // Limpiar bandera de Timer1?
    TMR1IE=1; // Habilitacion local de la interrupcion de Timer1?
    TMR1ON = 1;

    //Configuración iterrupción teclado (pueto B)
    TRISB=0b11110000; // Configura de RB0 a RB3 como salidas y de RB4 a RB7 como entradas
    LATB=0b00000000;// Salidas del puerto B = 0
    RBPU=0; //Activa resistencias de pull-up para el puerto B
    __delay_ms(100); //Delay mientras se polarizan las entradas
    RBIF=0; // Bandera a cero
    RBIE=1; // Activación de la interrupción de teclado

    PEIE=1; // Habilitar interrupciones de perifericos
    GIE=1;  //Habilitación global de las interrupciones

    //////////////////////////////////////////////////////////////////////////

    Bienvenida(); //Mensaje de Bienvenida en el LCD

    while(1){
        PreguntaAlUsuario();
        OcultarCursor();
        //Mensaje en pantalla
        MensajeLCD_Var("Faltantes: ");
        EscribeLCD_n8(Objetivo-Supercontador,2);
        DireccionaLCD(0xC0);    
        MensajeLCD_Var("Objetivo: ");
        EscribeLCD_n8(Objetivo,2);
        salir=1;
        //
        while (salir==1){
            if(Supercontador==Objetivo){ //Verifica si se llega al objetivo
                
                
                LATA2=1;
                __delay_ms(1000);
                LATA2=0;

                // Mensaje en pantalla
                BorraLCD();
                MensajeLCD_Var("Cuenta Cumplida");
                DireccionaLCD(0xC4);
                MensajeLCD_Var("Presione OK");
                //
                salir = 0; //Se hace la salida 0 para que se salga del ciclo de conteo
                Tecla='\0'; //valor vacio
                while(Tecla!= '*'){} // Espera de pulso ok
                ConfigVariables(); // Configuracion a valores iniciales

            }
            if(RC1==0&&Supercontador!=Objetivo){                                // Verifica si el interruptor está pulsado
                Inactividad=0;                                                  // Si esta contnado no quiero que entre en sleep
                Pulsado=0;  
            }

            if(Pulsado==0){
                
                if(RC1==1){
                    Pulsado=1;
                    contador++; // Aumenta contador
                    Supercontador++; // Aumenta Supercontador
                    if (contador==10){ // Unidades

                        LATA2=1;
                        __delay_ms(300);
                        LATA2=0;

                        contador=0;
                        contadorRGB++; // Aumento de decenas 
                        if (contadorRGB==6){ // Decenas 
                            contadorRGB=0;
                        }
                    }                
                    //RGB (decenas)
                    if(contadorRGB==0){
                        LATE=0b00000010; //Magenta
                    }else if(contadorRGB==1){
                        LATE=0b00000011; // Azul
                    }else if(contadorRGB==2){
                        LATE=0b00000001; // Cyan
                    }else if(contadorRGB==3){
                        LATE=0b00000101; // Verde
                    }else if(contadorRGB==4){
                        LATE=0b00000100; // Amarillo
                    }else if(contadorRGB==5){
                        LATE=0b00000000; //Blanco
                    }
                    //Mensaje en pantalla
                    DireccionaLCD(0x8B);
                    EscribeLCD_n8(Objetivo-Supercontador,2);
                    //
                    LATD=contador; // Salida del siete segmentos 
                    __delay_ms(500);  // delay pa' el pulsador   
                }
            }   
        }

        LATE=0b00000010; // Rgb (Magenta) 
        LATD=contador; // Salida siete segmentos
    }
}

void __interrupt()ISR(void){
    if(TMR0IF==1){ // Led de operación  
        TMR0=3036; //Valor de precarga
        TMR0IF=0; //Bandera en 0
        LATA1=LATA1^1; // Prende o apaga el led 
    }

    if (TMR1IF){
        TMR1 = 34286;    // Recargar
        TMR1IF = 0;

        Inactividad++;   // Contar segundos

        // Apagar backlight a los 10 s
        if(Inactividad == 10){
            //Backlight = 0;
            LATA3 = 0;
        }

        // Entrar en suspensión a los 20 s
        if(Inactividad >= 20){
            Sleep();    // suspender PIC

            // ---- DESPERTÓ AQUI ----
            Inactividad = 0;             // reiniciar inactividad
            RBIF = 0;                    // limpiar interrupción por teclado
            TMR1ON = 1;                  // volver a encender Timer1
        }
    }

    if(RBIF==1){
        if(PORTB!=0b11110000){   
            Inactividad = 0;                                                    // Hubo actividad
            LATB=0b11111110;
            if(RB4==0){                                                         //1
                Tecla=1; 
                ConfigPregunta();
            }            
            else if(RB5==0) {                                                   //2
                Tecla=2; 
                ConfigPregunta();

            }
            else if(RB6==0){                                                    //3
                Tecla=3; 
                ConfigPregunta();

            }
            else if(RB7==0){                                                    //OK
                Tecla='*'; 
            }




            else{
                LATB=0b11111101;
                if(RB4==0){                                                     //4
                Tecla=4;
                ConfigPregunta();
                }
                else if(RB5==0) {                                               //5
                    Tecla=5; 
                    ConfigPregunta();
                }
                else if(RB6==0) {                                               //6
                    Tecla=6; 
                    ConfigPregunta();
                }
                else if(RB7==0) {                                               //PARADA EMERGENCIA
                    LATE=0b00000110; //Led en rojo
                    //Mensaje en pantalla
                    BorraLCD(); 
                    OcultarCursor();
                    MensajeLCD_Var("    PARADA DE");
                    DireccionaLCD(0xC2);
                    MensajeLCD_Var("EMERGENCIA");
                    //
                    while(1){} // Bucle
                }   


            else{
                LATB=0b11111011;
                if(RB4==0) {                                                    //1
                    Tecla=7; 
                    ConfigPregunta();
                }
                else if(RB5==0) {                                               //2
                    Tecla=8; 
                    ConfigPregunta();
                }
                else if(RB6==0) {                                               //3
                    Tecla=9; 
                    ConfigPregunta();
                } 
                else if(RB7==0) {                                               //SUPR
                    Borrar();
                }



            else{
                LATB=0b11110111;                                                //REINICIO
                if(RB4==0){ 
                    contador=0;
                    Supercontador = 0;
                    contadorRGB = 0;
                    LATE=0b00000010; // Rgb (Magenta) 
                    if(salir==1){
                        DireccionaLCD(0x8B);
                        EscribeLCD_n8(Objetivo-Supercontador,2); //Mensaje pantalla
                        LATD=contador; //Salida del siete segmentos
                    }
                }
                else if(RB5==0) {                                               //0
                    Tecla=0; 
                    ConfigPregunta();
                }
                else if(RB6==0) {                                               //FIN
                    Borrar();
                    Supercontador=Objetivo;
                    contadorRGB = Objetivo/10;
                    contador = Objetivo-contadorRGB*10;
                    if(contadorRGB==0){
                        LATE=0b00000010; //Magenta
                    }else if(contadorRGB==1){
                        LATE=0b00000011; // Azul
                    }else if(contadorRGB==2){
                        LATE=0b00000001; // Cyan
                    }else if(contadorRGB==3){
                        LATE=0b00000101; // Verde
                    }else if(contadorRGB==4){
                        LATE=0b00000100; // Amarillo
                    }else if(contadorRGB==5){
                        LATE=0b00000000; //Blanco
                    }
                    LATD=contador; // Salida del siete segmentos
                }
                else if(RB7==0) {      //LUZ
                    LATA3=LATA3^1;
                    TMR1ON=1; // Encender Timer1
                }
            }
            }
            }
            LATB=0b11110000; // configuración default
        }
        __delay_ms(300); // delay pa' el pulsador 
        RBIF=0; // Bandera en 0
    }
}
void ConfigVariables(void){ //Valores iniciales de la variables
    Pulsado=1;
    contador=0; 
    salir=0;
    Supercontador=0;
    contadorRGB=0;
    posicion=0;
    Objetivo=0;
    Tecla='\0'; 
}
void Bienvenida(void){
    // CONFIGURACIÓN DEL LCD
    ConfiguraLCD(4); //Modo de bits  
    InicializaLCD(); //Inicialización de la pantalla
    OcultarCursor();

    //CREACIÓN DE CARACTERES PROPIOS
    CrearCaracter(Corazon, 0);

    //Mensaje en pantalla
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    MensajeLCD_Var(" Bienvenido ");
    DireccionaLCD(0xC0);
    EscribeLCD_c(0);
    EscribeLCD_c(0);
    MensajeLCD_Var("  Usuario ");
    __delay_ms(3200);

    // ? Mover a la derecha (aprox 2 s)
    for(int i = 0; i < 18; i++){
        DesplazaPantallaD();  // Shift right
        __delay_ms(100);
    }

    // Mensaje total < 5 segundos
}
void PreguntaAlUsuario(void){ //encargada del setup de la pregunta de elementos a contar
    while(1){
        CrearCaracter(RayaAlPiso, 1);
        posicion=0; //posicion del cursor en unidades
        //Mensaje en pantalla
        BorraLCD();
        MensajeLCD_Var("Piezas a contar:");
        DireccionaLCD(0xC7);
        EscribeLCD_c(1);
        EscribeLCD_c(1);
        DireccionaLCD(0xC7);
        MostrarCursor();
        //
        editor=1; // El usuario puede escribir en el lcd 
        while(Tecla!= '*'){ // no hacer nada mientras no se de al ok  
        // el ingreso del valor a contar se hace por medio de interrupciones
        }
        if((Objetivo>59)||(Objetivo==0)){
            editor=0; // El usuario no puede escribir en el lcd
            Tecla='\0';
            Objetivo=0;
            //Mensaje en pantalla
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
            //
        }else{
        editor=0; // El usuario no puede escribir en el lcd
        posicion='0';
        BorraLCD();
        break;
            }
    }
}
void ConfigPregunta(){ //Funcion para el ingreso del valor a contar
    if(posicion==0 && editor==1){
        EscribeLCD_n8(Tecla,1); //Mensaje en pantalla
        Objetivo=Tecla;
    }else if(posicion==1 && editor==1){
        EscribeLCD_n8(Tecla,1); //Mensaje en pantalla
        Objetivo=Objetivo*10+Tecla;
        OcultarCursor();
    }
    posicion++; 
}
void Borrar(){ //Borrar el valor escrito por el usuario 
    if(editor==1){
        MostrarCursor();
        Objetivo=0;
        posicion=0;
        DireccionaLCD(0xC7);
        EscribeLCD_c(1);
        EscribeLCD_c(1);
        DireccionaLCD(0xC7);
    }
}