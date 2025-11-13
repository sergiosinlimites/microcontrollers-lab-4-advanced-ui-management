#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#define _XTAL_FREQ 4700000UL   // Frecuencia para delays y TMR0 1 Hz

// ==================== CONFIG ====================
#pragma config WDT=OFF
#pragma config LVP=OFF
#pragma config PBADEN=OFF
#pragma config MCLRE=ON
#pragma config FOSC=HS

// ===== Parámetros =====
#define BEEP_PERIOD_US 250u

// ===== I/O =====
// LED blink (RA1) y buzzer (RA2)
#define CLK_TRIS  TRISA1
#define CLK_LAT   LATA1
#define BUZZ_TRIS TRISA2
#define BUZZ_LAT  LATA2

// Sensor conteo (RC1)
#define COUNTER_TRIS  TRISC1
#define BTN_RC1_PORT  RC1

// ---- LCD 16x2 (4 bits): D4..D7=RD4..RD7, RS=RA4, EN=RA5 ----
#define LCD_RS_TRIS TRISA4
#define LCD_RS_LAT  LATA4
#define LCD_EN_TRIS TRISA5
#define LCD_EN_LAT  LATA5
#define LCD_D_TRIS  TRISD
#define LCD_D_LAT   LATD  // RD4..RD7

// ---- Teclado 4x4 en PORTB ----
// Filas: RB0..RB3 (salidas); Columnas: RB4..RB7 (entradas con pull-up)
#define KB_ROWS_TRIS TRISB
#define KB_ROWS_LAT  LATB
#define KB_COL_MASK  0xF0    // columnas RB7..RB4

// ===== TMR0 preloads (1 Hz en RA1, toggle cada 0.5 s) =====
#define TMR0_PRELOAD_H           0x70
#define TMR0_PRELOAD_L           0x91

// ===== Estado / Teclado =====
volatile uint8_t started = 0;
volatile uint8_t finished = 0;
volatile uint8_t emg_latched = 0;     // STOP latcheado hasta MCLR
volatile uint8_t Tecla = 0;           // 0=sin tecla, 1..16 = tecla detectada

// ===== Variables de UI/contador =====
static uint8_t target = 0;        // 1..59
static uint8_t count  = 0;        // 0..target
static uint8_t input_digits = 0;  // 0,1,2
static uint8_t input_val    = 0;  // 0..99

// ===== Prototipos =====
void tmr0_start(void);
void beep_ms(uint16_t ms);

// LCD
void lcd_init(void);
void lcd_clear(void);
void lcd_gotoxy(uint8_t col, uint8_t row);
void lcd_putc(char c);
void lcd_print(const char *s);
static void lcd_print2d(uint8_t v);

// Teclado
char keypad_map_to_char(uint8_t k);  // a ?0?..?9?,?A?,?B?,?C?,?D?, ?*?, ?#?

// UI
void ui_show_welcome(void);
void ui_show_input(void);
void ui_show_input_value(uint8_t val, uint8_t digits);
void ui_show_counting(void);
void ui_show_finished(void);
void ui_show_emergency(void);

// Lógica
bool validate_target(uint8_t v);
void reset_counter_only(void);
void reset_all_to_input(void);

// ==================== ISR (baja prioridad) ====================
void __interrupt(low_priority) low_isr(void){
    // --- PORTB change: teclado ---
    if (INTCONbits.RBIF){
        Tecla = 0;

        // Barrido por filas (modificando SOLO nibble bajo)
        KB_ROWS_LAT = (KB_ROWS_LAT & 0xF0) | 0b00001110; // Fila 1 activa
        if(RB4==0) Tecla=1;
        else if(RB5==0) Tecla=2;
        else if(RB6==0) Tecla=3;
        else if(RB7==0) Tecla=4;
        else{
            KB_ROWS_LAT = (KB_ROWS_LAT & 0xF0) | 0b00001101; // Fila 2
            if(RB4==0) Tecla=5;
            else if(RB5==0) Tecla=6;
            else if(RB6==0) Tecla=7;
            else if(RB7==0) Tecla=8;
            else{
                KB_ROWS_LAT = (KB_ROWS_LAT & 0xF0) | 0b00001011; // Fila 3
                if(RB4==0) Tecla=9;
                else if(RB5==0) Tecla=10;
                else if(RB6==0) Tecla=11;
                else if(RB7==0) Tecla=12;
                else{
                    KB_ROWS_LAT = (KB_ROWS_LAT & 0xF0) | 0b00000111; // Fila 4
                    if(RB4==0) Tecla=13;
                    else if(RB5==0) Tecla=14;
                    else if(RB6==0) Tecla=15;
                    else if(RB7==0) Tecla=16;
                }
            }
        }

        // Reposo: filas en 0 (para que próxima pulsación baje columna y dispare RBIF)
        KB_ROWS_LAT = (KB_ROWS_LAT & 0xF0) | 0x00;

        __delay_ms(12);     // antirrebote simple (como el demo)
        (void)PORTB;        // limpia mismatch
        INTCONbits.RBIF = 0;
    }

    // --- TMR0: blink 1 Hz ---
    if (INTCONbits.TMR0IF){
        INTCONbits.TMR0IF = 0;
        TMR0H = TMR0_PRELOAD_H;
        TMR0L = TMR0_PRELOAD_L;
        CLK_LAT ^= 1;
    }
}

// ==================== MAIN ====================
void main(void){
    // Todo digital
    ADCON1 = 0x0F;

    // RA: LED, buzzer, LCD RS/EN
    TRISA &= ~((1<<1)|(1<<2)|(1<<4)|(1<<5)); // RA1,RA2,RA4,RA5 salidas
    LATA  &= ~((1<<1)|(1<<2)|(1<<4)|(1<<5));
    // RC1 sensor
    COUNTER_TRIS = 1;

    // PORTD para LCD D4..D7 (salidas)
    LCD_D_TRIS &= 0x0F;  // RD7..RD4 = 0

    // PORTB: columnas in (RB4..RB7), filas out (RB0..RB3)
    TRISB = 0b11110000;
    KB_ROWS_LAT = (KB_ROWS_LAT & 0xF0) | 0x00; // filas en 0 (reposo)
    INTCON2bits.RBPU = 0;   // pull-ups internos activos (columnas)

    // Prioridades e interrupciones
    RCONbits.IPEN   = 1;    // prioridades
    INTCONbits.GIEH = 1;    // global high
    INTCONbits.GIEL = 1;    // global low

    // RBIF como baja prioridad
    INTCON2bits.RBIP = 0;
    (void)PORTB;            // lectura inicial
    INTCONbits.RBIF = 0;
    INTCONbits.RBIE = 1;

    // TMR0
    tmr0_start();

    // LCD
    lcd_init();

    // Estado inicial
    started = 0; finished = 0; emg_latched = 0;
    target = 0; count = 0; input_digits = 0; input_val = 0;

    ui_show_welcome();
    __delay_ms(5000);

    reset_all_to_input();
    ui_show_input();

    uint8_t last_rc1 = 1;

    while(1){
        if (emg_latched){
            ui_show_emergency();
            __delay_ms(40);
            continue;
        }

        // Tecla detectada por ISR
        if (Tecla){
            char kc = keypad_map_to_char(Tecla);
            Tecla = 0;

            // STOP (B) = EMG latcheada
            if (kc == 'B'){ emg_latched = 1; continue; }

            if (!started && !finished){
                // Ingreso de objetivo
                if (kc >= '0' && kc <= '9'){
                    if (input_digits < 2){
                        input_val = (uint8_t)(input_val*10 + (kc - '0'));
                        input_digits++;
                        ui_show_input_value(input_val, input_digits);
                    } else { beep_ms(70); }
                } else if (kc == 'C'){ // SUPR
                    if (input_digits){
                        input_digits--; input_val /= 10;
                        ui_show_input_value(input_val, input_digits);
                    } else { beep_ms(70); }
                } else if (kc == 'A'){ // OK
                    if (validate_target(input_val)){
                        target = input_val; count = 0;
                        started = 1; finished = 0;
                        ui_show_counting();
                    } else {
                        lcd_clear(); lcd_print("Objetivo inval.");
                        lcd_gotoxy(0,1); lcd_print("Rango 1..59");
                        beep_ms(200); __delay_ms(900);
                        ui_show_input();
                        input_digits = 0; input_val = 0;
                    }
                } else if (kc == '*'){ // RST en input = limpiar entrada
                    input_digits = 0; input_val = 0; ui_show_input();
                } // '#' en input: no-op
            }
            else if (started && !finished){
                if (kc == '*'){       // RST: reiniciar conteo a 0
                    reset_counter_only(); ui_show_counting();
                } else if (kc == '#'){ // END: abortar y volver a pedir objetivo
                    reset_all_to_input(); ui_show_input();
                }
            }
            else if (finished){
                if (kc == 'A' || kc == '*' || kc == '#'){
                    reset_all_to_input(); ui_show_input();
                }
            }
        }

        // Sensor conteo RC1 (flanco 1->0)
        uint8_t rc1 = BTN_RC1_PORT;
        if (started && !finished && (last_rc1==1 && rc1==0)){
            if (count < 59) count++;
            ui_show_counting();
            if (target && count >= target){
                finished = 1; started = 0; ui_show_finished();
            }
            __delay_ms(30); // antirrebote
        }
        last_rc1 = rc1;

        __delay_ms(8);
    }
}

// ==================== TMR0 ====================
void tmr0_start(void){
    T0CON = 0b00000011; // 16 bits, clk interno, prescaler 1:16
    TMR0H = TMR0_PRELOAD_H;
    TMR0L = TMR0_PRELOAD_L;
    INTCON2bits.TMR0IP = 0; // baja prioridad
    INTCONbits.TMR0IF = 0;
    INTCONbits.TMR0IE = 1;
    T0CONbits.TMR0ON = 1;
}

// ==================== Utils ====================
void beep_ms(uint16_t ms){
    uint32_t n = ((uint32_t)ms*1000u)/(2u*BEEP_PERIOD_US);
    while(n--){
        BUZZ_LAT=1; __delay_us(BEEP_PERIOD_US);
        BUZZ_LAT=0; __delay_us(BEEP_PERIOD_US);
    }
}

// ==================== LCD (4 bits) ====================
static void lcd_pulse(void){ LCD_EN_LAT=1; __delay_us(2); LCD_EN_LAT=0; __delay_us(50); }
static void lcd_write4(uint8_t nib){ uint8_t out=(nib&0x0F)<<4; LCD_D_LAT=(LCD_D_LAT&0x0F)|out; lcd_pulse(); }
static void lcd_write(uint8_t b, uint8_t rs){ LCD_RS_LAT=rs?1:0; lcd_write4(b>>4); lcd_write4(b&0x0F); }
static void lcd_cmd(uint8_t c){ lcd_write(c,0); if(c==0x01||c==0x02) __delay_ms(2); else __delay_us(50); }

void lcd_init(void){
    __delay_ms(20);
    LCD_RS_LAT=0; LCD_EN_LAT=0;

    // 4-bit init
    lcd_write4(0x03); __delay_ms(5);
    lcd_write4(0x03); __delay_us(150);
    lcd_write4(0x03); __delay_us(150);
    lcd_write4(0x02); __delay_us(150);

    lcd_cmd(0x28); // 4-bit, 2 líneas, 5x8
    lcd_cmd(0x08); // display off
    lcd_cmd(0x01); // clear
    lcd_cmd(0x06); // entry inc
    lcd_cmd(0x0C); // display on, cursor off
}
void lcd_clear(void){ lcd_cmd(0x01); }
void lcd_gotoxy(uint8_t col, uint8_t row){ lcd_cmd(0x80 | ((row?0x40:0x00)+col)); }
void lcd_putc(char c){ lcd_write((uint8_t)c,1); }
void lcd_print(const char *s){ while(*s) lcd_putc(*s++); }
static void lcd_print2d(uint8_t v){ lcd_putc('0'+(v/10)); lcd_putc('0'+(v%10)); }

// ==================== Teclado ====================
char keypad_map_to_char(uint8_t k){
    // Layout: A=OK, B=STOP, C=SUPR, D=SCRN; *=RST, #=END
    static const char map[16] = {
        '7','8','9','A',
        '4','5','6','B',
        '1','2','3','C',
        '*','0','#','D'
    };
    if (k>=1 && k<=16) return map[k-1];
    return 0;
}

// ==================== UI ====================
void ui_show_welcome(void){
    lcd_clear();
    lcd_print("Bienvenido!");
    lcd_gotoxy(0,1); lcd_print("Sistema conteo");
}
void ui_show_input(void){
    lcd_clear();
    lcd_print("Piezas a contar:");
    lcd_gotoxy(0,1); lcd_print("Rango 1..59");
}
void ui_show_input_value(uint8_t val, uint8_t digits){
    lcd_clear();
    lcd_print("Piezas a contar: ");
    if (digits==0){ lcd_putc('-'); lcd_putc('-'); }
    else if (digits==1){ lcd_putc('0'+(val%10)); }
    else { lcd_print2d(val); }
    lcd_gotoxy(0,1); lcd_print("OK=Comenzar  SUPR=Eliminar");
}
void ui_show_counting(void){
    lcd_clear();
    lcd_print("Piezas faltantes: "); uint8_t r=(target>count)?(target-count):0; lcd_print2d(r);
    lcd_gotoxy(0,1);
    lcd_print("Cuenta objetivo:");  lcd_print2d(target);
}
void ui_show_finished(void){
    lcd_clear();
    lcd_print(">> Objetivo OK <<");
    lcd_gotoxy(0,1); lcd_print("OK=Nueva cuenta, RST/*, END/#");
    beep_ms(400);
}
void ui_show_emergency(void){
    lcd_clear();
    lcd_print("!! EMERGENCIA !!");
    lcd_gotoxy(0,1); lcd_print("Reinicie con MCLR");
}

// ==================== Lógica ====================
bool validate_target(uint8_t v){ return (v>=1 && v<=59); }
void reset_counter_only(void){ count=0; finished=0; }
void reset_all_to_input(void){
    started=0; finished=0; emg_latched=0;
    target=0; count=0; input_digits=0; input_val=0;
}
