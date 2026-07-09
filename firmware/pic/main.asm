;==============================================================================
; SMART HOME CONTROLLER
;
; Target  : PIC18F4550  @ 4 MHz XT crystal   (Fcy = 1 MHz)
; Toolchain: Microchip MPASM
;
; Peripherals : 16x2 LCD (4-bit), DHT11, LDR (ADC), PIR, 4-key keypad,
;               servo lock, fan/buzzer relays, EUSART link to ESP32.
;==============================================================================

        LIST    P=18F4550, R=DEC
        #include <p18f4550.inc>

;==============================================================================
; DEVICE CONFIGURATION
;==============================================================================

        CONFIG  FOSC    = XT_XT
        CONFIG  PLLDIV  = 1
        CONFIG  CPUDIV  = OSC1_PLL2
        CONFIG  USBDIV  = 1
        CONFIG  FCMEN   = OFF
        CONFIG  IESO    = OFF
        CONFIG  PWRT    = ON
        CONFIG  BOR     = ON
        CONFIG  VREGEN  = OFF
        CONFIG  WDT     = OFF
        CONFIG  MCLRE   = ON
        CONFIG  LPT1OSC = OFF
        CONFIG  PBADEN  = OFF
        CONFIG  CCP2MX  = ON
        CONFIG  STVREN  = ON
        CONFIG  LVP     = OFF
        CONFIG  ICPRT   = OFF
        CONFIG  XINST   = OFF
        CONFIG  DEBUG   = OFF
        CONFIG  CP0     = OFF
        CONFIG  CP1     = OFF
        CONFIG  CP2     = OFF
        CONFIG  CP3     = OFF
        CONFIG  CPB     = OFF
        CONFIG  CPD     = OFF
        CONFIG  WRT0    = OFF
        CONFIG  WRT1    = OFF
        CONFIG  WRT2    = OFF
        CONFIG  WRT3    = OFF
        CONFIG  WRTB    = OFF
        CONFIG  WRTC    = OFF
        CONFIG  WRTD    = OFF
        CONFIG  EBTR0   = OFF
        CONFIG  EBTR1   = OFF
        CONFIG  EBTR2   = OFF
        CONFIG  EBTR3   = OFF
        CONFIG  EBTRB   = OFF

;==============================================================================
; SYSTEM CONSTANTS
;==============================================================================

FOSC            EQU     D'4000000'
FCY             EQU     D'1000000'
BAUD_RATE       EQU     D'9600'
TICK_HZ         EQU     D'100'

; Operating modes
MODE_HOME       EQU     0
MODE_AWAY       EQU     1

; Activity-log event types (EEPROM 0x10-0x17)
LOG_BOOT        EQU     1
LOG_PIN_OK      EQU     2
LOG_PIN_FAIL    EQU     3
LOG_INTRUSION   EQU     4
LOG_ARMED       EQU     5
LOG_DISARMED    EQU     6
LOG_MODE_HOME   EQU     7
LOG_MODE_AWAY   EQU     8
LOG_HOTALERT    EQU     9
LOG_HUMALERT    EQU     D'10'

; sys_flags bit positions
SF_T10MS        EQU     0
SF_T100MS       EQU     1
SF_T1S          EQU     2
SF_RX_READY     EQU     3

; Servo pulse widths (x10 us)
SERVO_LOCKED    EQU     D'200'
SERVO_UNLOCKED  EQU     D'100'

;==============================================================================
; DATA MEMORY  (ACCESS BANK)
;==============================================================================

        CBLOCK  H'020'
            sys_tick_lo
            sys_tick_hi
            sys_flags
            sys_mode

            temp_x10
            hum_pct
            ldr_val

            fan_on
            door_unlocked
            alarm_armed

            isr_w_save
            isr_status_save
            isr_bsr_save

            tick_100ms_cnt
            tick_1s_cnt

            lcd_tmp
            lcd_delay_lo
            lcd_delay_hi
            lcd_arg
            lcd_num_d100
            lcd_num_d10

            seconds_count
            d50_cnt
            d2ms_outer
            d2ms_inner
            d20ms_cnt
            num_val
            adc_lo
            adc_hi
            smooth_inst
            sample_idx
            sum_lo
            sum_hi
            samples
            samples1
            samples2
            samples3
            samples4
            samples5
            samples6
            samples7
            avg_lo
            avg_hi
            pwm_cnt
            pwm_duty
            rx_byte
            dht_ok
            dht_interval
            dht_timeout
            dht_byte_cnt
            dht_bit_cnt
            dht_d_outer
            dht_d_inner
            dht_b0
            dht_b1
            dht_b2
            dht_b3
            dht_b4
            servo_pulse_us
            servo_frame_cnt
            servo_dl_lo
            alarm_triggered
            cmd_buf
            cmd_buf_r1
            cmd_buf_r2
            cmd_buf_r3
            cmd_buf_r4
            cmd_buf_r5
            cmd_idx
            ir_count
            ee_addr
            ee_data
            pin_stored0
            pin_stored1
            pin_stored2
            pin_stored3
            pin_entry0
            pin_entry1
            pin_entry2
            pin_entry3
            pin_pos
            pin_attempts
            key_now
            key_last
            key_new
            key_pressed
            wrong_beep_timer
            pin_active_timer
            led_override
            led_override_duty
            relock_timer
            presence_timer
            presence_state
            led_tmp
            fan_override
            ctrl_mode

            welcome_timer
            hot_alert_sent
            hum_alert_sent
            log_head
            log_event_tmp
        ENDC

;==============================================================================
; RESET / INTERRUPT VECTORS
;==============================================================================

        ORG     H'0000'
        goto    Start

        ORG     H'0008'
        goto    ISR_High

        ORG     H'0018'
        goto    ISR_Low

        ORG     H'0020'

;==============================================================================
; PROGRAM ENTRY
;==============================================================================

;------------------------------------------------------------------------------
; Start  :  program entry, peripheral bring-up, LCD splash
;------------------------------------------------------------------------------
Start:
        call    Init_Ports
        call    Init_Variables
        call    Init_ADC
        call    EE_LoadPIN
        call    Log_Init

        call    LCD_Init
        call    LCD_Clear

        movlw   H'00'
        call    LCD_Goto
        movlw   'S'
        call    LCD_Char
        movlw   'm'
        call    LCD_Char
        movlw   'a'
        call    LCD_Char
        movlw   'r'
        call    LCD_Char
        movlw   't'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   'H'
        call    LCD_Char
        movlw   'o'
        call    LCD_Char
        movlw   'm'
        call    LCD_Char
        movlw   'e'
        call    LCD_Char

        movlw   H'40'
        call    LCD_Goto
        movlw   'L'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char

        movlw   H'46'
        call    LCD_Goto
        movlw   'I'
        call    LCD_Char
        movlw   'R'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char

        movlw   H'4B'
        call    LCD_Goto
        movlw   'S'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char

        clrf    seconds_count
        clrf    door_unlocked

        movlw   H'4D'
        call    LCD_Goto
        movlw   'L'
        call    LCD_Char

        call    Init_Timer0
        call    Init_Timer2
        call    Init_UART
        call    Init_Interrupts

        goto    Main_Loop

;==============================================================================
; INITIALISATION
;==============================================================================

;------------------------------------------------------------------------------
; Init_Ports  :  set TRIS / LAT direction for ports A-E
;------------------------------------------------------------------------------
Init_Ports:
        clrf    LATA
        movlw   B'00000011'
        movwf   TRISA

        clrf    LATB
        movlw   B'11110011'
        movwf   TRISB
        clrf    LATC
        movlw   B'10000000'
        movwf   TRISC
        clrf    LATD
        movlw   B'00000000'
        movwf   TRISD
        clrf    LATE
        movlw   B'00000000'
        movwf   TRISE
        return

;------------------------------------------------------------------------------
; Init_Variables  :  initialise all RAM state to defaults
;------------------------------------------------------------------------------
Init_Variables:
        clrf    sys_tick_lo
        clrf    sys_tick_hi
        clrf    sys_flags
        movlw   MODE_HOME
        movwf   sys_mode
        clrf    alarm_armed
        clrf    door_unlocked
        clrf    fan_on
        clrf    ldr_val

        clrf    samples
        clrf    samples1
        clrf    samples2
        clrf    samples3
        clrf    samples4
        clrf    samples5
        clrf    samples6
        clrf    samples7
        clrf    sample_idx
        clrf    sum_lo
        clrf    sum_hi
        clrf    pwm_cnt
        clrf    pwm_duty
        clrf    rx_byte
        clrf    dht_ok
        movlw   D'1'
        movwf   dht_interval

        movlw   SERVO_LOCKED
        movwf   servo_pulse_us
        movlw   D'2'
        movwf   servo_frame_cnt
        clrf    servo_dl_lo
        clrf    alarm_triggered
        clrf    cmd_idx
        clrf    ir_count

        clrf    pin_entry0
        clrf    pin_entry1
        clrf    pin_entry2
        clrf    pin_entry3
        clrf    pin_pos
        clrf    pin_attempts
        clrf    key_last
        clrf    wrong_beep_timer
        clrf    pin_active_timer

        clrf    led_override
        clrf    led_override_duty
        clrf    ctrl_mode
        clrf    fan_override
        clrf    relock_timer
        movlw   D'5'
        movwf   presence_timer
        clrf    presence_state

        clrf    welcome_timer
        clrf    hot_alert_sent
        clrf    hum_alert_sent
        clrf    log_head
        movlw   D'10'
        movwf   tick_100ms_cnt
        movlw   D'10'
        movwf   tick_1s_cnt
        return

;------------------------------------------------------------------------------
; Init_ADC  :  configure ADC (right-justified, Fosc/4, AN0-AN1)
;------------------------------------------------------------------------------
Init_ADC:
        movlw   B'00001101'
        movwf   ADCON1
        movlw   B'10010100'
        movwf   ADCON2
        movlw   B'00000101'
        movwf   ADCON0
        return

;==============================================================================
; ANALOG INPUT (LDR)
;==============================================================================

;------------------------------------------------------------------------------
; ADC_Read_AN1  :  sample AN1, scale to 8-bit, 8-tap average -> ldr_val
;------------------------------------------------------------------------------
ADC_Read_AN1:

        movlw   B'00000101'
        movwf   ADCON0
        nop
        nop
        bsf     ADCON0,GO
ADC_Wait:
        btfsc   ADCON0,GO
        bra     ADC_Wait

        movff   ADRESL, adc_lo
        movff   ADRESH, adc_hi

        movff   adc_lo, smooth_inst

        bcf     STATUS,C
        rrcf    adc_hi,W
        rrcf    smooth_inst,F

        movff   adc_hi, avg_hi
        movff   adc_lo, smooth_inst
        bcf     STATUS,C
        rrcf    avg_hi,F
        rrcf    smooth_inst,F
        bcf     STATUS,C
        rrcf    avg_hi,F
        rrcf    smooth_inst,F

        movf    smooth_inst,W
        mullw   D'101'
        movff   PRODH, smooth_inst

        movlw   D'100'
        cpfsgt  smooth_inst
        bra     ADC_PctOK
        movwf   smooth_inst
ADC_PctOK:

        movf    smooth_inst,W
        sublw   D'100'
        movwf   smooth_inst

        lfsr    FSR0, samples
        movf    sample_idx,W
        addwf   FSR0L,F
        movf    INDF0,W
        subwf   sum_lo,F
        btfss   STATUS,C
        decf    sum_hi,F
        movff   smooth_inst, INDF0
        movf    smooth_inst,W
        addwf   sum_lo,F
        btfsc   STATUS,C
        incf    sum_hi,F

        incf    sample_idx,F
        movlw   B'00000111'
        andwf   sample_idx,F

        movff   sum_lo, avg_lo
        movff   sum_hi, avg_hi
        bcf     STATUS,C
        rrcf    avg_hi,F
        rrcf    avg_lo,F
        bcf     STATUS,C
        rrcf    avg_hi,F
        rrcf    avg_lo,F
        bcf     STATUS,C
        rrcf    avg_hi,F
        rrcf    avg_lo,F
        movff   avg_lo, ldr_val
        return

;==============================================================================
; TIMER 0  (SYSTEM TICK)
;==============================================================================

;------------------------------------------------------------------------------
; Init_Timer0  :  10 ms periodic tick, high priority
;------------------------------------------------------------------------------
Init_Timer0:
        movlw   B'10000001'
        movwf   T0CON
        movlw   H'F6'
        movwf   TMR0H
        movlw   H'3C'
        movwf   TMR0L
        bcf     INTCON,TMR0IF
        bsf     INTCON2,TMR0IP
        bsf     INTCON,TMR0IE
        return

;==============================================================================
; UART COMMAND PARSER
;==============================================================================

;------------------------------------------------------------------------------
; Parse_Char  :  buffer one received byte into the command line
;------------------------------------------------------------------------------
Parse_Char:
        movf    rx_byte,W
        sublw   D'13'
        bz      Parse_End
        movf    rx_byte,W
        sublw   D'10'
        bz      Parse_End
        movf    rx_byte,W
        sublw   '$'
        bnz     PC_StoreMaybe

        clrf    cmd_idx
        movlw   '$'
        movwf   cmd_buf
        incf    cmd_idx,F
        return
PC_StoreMaybe:

        movf    cmd_idx,F
        bz      Parse_Done

        lfsr    FSR0, cmd_buf
        movf    cmd_idx,W
        addwf   FSR0L,F
        movf    rx_byte,W
        movwf   INDF0
        incf    cmd_idx,F

        movlw   D'8'
        cpfslt  cmd_idx
        clrf    cmd_idx
Parse_Done:
        return

;------------------------------------------------------------------------------
; Parse_End  :  dispatch a completed '$' command
;------------------------------------------------------------------------------
Parse_End:

        movf    cmd_idx,F
        bnz     PE_HaveData
        goto    Parse_Reset
PE_HaveData:

        movf    cmd_buf,W
        sublw   '$'
        bz      PE_GoodStart
        goto    Parse_Reset
PE_GoodStart:

        movf    cmd_buf+1,W
        sublw   'D'
        bnz     Disp_NotD
        goto    Cmd_Door
Disp_NotD:
        movf    cmd_buf+1,W
        sublw   'A'
        bnz     Disp_NotA
        goto    Cmd_Alarm
Disp_NotA:
        movf    cmd_buf+1,W
        sublw   'M'
        bnz     Disp_NotM
        goto    Cmd_Mode
Disp_NotM:
        movf    cmd_buf+1,W
        sublw   'F'
        bnz     Disp_NotF
        goto    Cmd_Fan
Disp_NotF:
        movf    cmd_buf+1,W
        sublw   'P'
        bnz     Disp_NotP
        goto    Cmd_PIN
Disp_NotP:
        movf    cmd_buf+1,W
        sublw   'L'
        bnz     Disp_NotL
        goto    Cmd_LED
Disp_NotL:
        movf    cmd_buf+1,W
        sublw   'X'
        bnz     Disp_NotX
        goto    Cmd_AutoMode
Disp_NotX:
        movf    cmd_buf+1,W
        sublw   'Y'
        bnz     Disp_NotY
        goto    Cmd_Ctrl
Disp_NotY:
        movf    cmd_buf+1,W
        sublw   'R'
        bnz     Disp_NotR
        goto    Cmd_Report
Disp_NotR:
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_Door  :  $D0 / $D1  lock / unlock servo
;------------------------------------------------------------------------------
Cmd_Door:
        movf    cmd_buf+2,W
        sublw   '1'
        bz      Cmd_DoorUnlock
        clrf    door_unlocked

        movlw   SERVO_LOCKED
        movwf   servo_pulse_us
        goto    Parse_Reset
Cmd_DoorUnlock:
        movlw   D'1'
        movwf   door_unlocked

        movlw   SERVO_UNLOCKED
        movwf   servo_pulse_us
        movlw   D'5'
        movwf   relock_timer
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_Alarm  :  $AA / $AD  arm / disarm alarm
;------------------------------------------------------------------------------
Cmd_Alarm:
        movf    cmd_buf+2,W
        sublw   'A'
        bz      Cmd_AlarmArm
        movf    cmd_buf+2,W
        sublw   'D'
        bz      Cmd_AlarmDisarm
        goto    Parse_Reset
Cmd_AlarmDisarm:
        clrf    alarm_armed
        clrf    alarm_triggered
        movlw   LOG_DISARMED
        call    Log_Event
        goto    Parse_Reset
Cmd_AlarmArm:
        movlw   D'1'
        movwf   alarm_armed
        movlw   LOG_ARMED
        call    Log_Event
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_Mode  :  $MH / $MA  set HOME / AWAY mode
;------------------------------------------------------------------------------
Cmd_Mode:
        movf    cmd_buf+2,W
        sublw   'H'
        bz      Cmd_ModeHome
        movf    cmd_buf+2,W
        sublw   'A'
        bz      Cmd_ModeAway
        goto    Parse_Reset
Cmd_ModeHome:
        movlw   MODE_HOME
        movwf   sys_mode
        clrf    alarm_armed
        clrf    alarm_triggered
        movlw   LOG_MODE_HOME
        call    Log_Event
        goto    Parse_Reset
Cmd_ModeAway:
        movlw   MODE_AWAY
        movwf   sys_mode
        movlw   D'1'
        movwf   alarm_armed
        movlw   LOG_MODE_AWAY
        call    Log_Event
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_PIN  :  $Pnnnn  dashboard PIN check
;------------------------------------------------------------------------------
Cmd_PIN:

        call    LCD_Clear
        movlw   H'00'
        call    LCD_Goto
        movlw   'A'
        call    LCD_Char
        movlw   'P'
        call    LCD_Char
        movlw   'P'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   'P'
        call    LCD_Char
        movlw   'I'
        call    LCD_Char
        movlw   'N'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char
        movf    cmd_buf+2,W
        call    LCD_Char
        movf    cmd_buf+3,W
        call    LCD_Char
        movf    cmd_buf+4,W
        call    LCD_Char
        movf    cmd_buf+5,W
        call    LCD_Char
        movlw   D'3'
        movwf   pin_active_timer
        movf    cmd_buf+2,W
        addlw   D'256' - '0'
        subwf   pin_stored0,W
        bnz     Cmd_PIN_Fail
        movf    cmd_buf+3,W
        addlw   D'256' - '0'
        subwf   pin_stored1,W
        bnz     Cmd_PIN_Fail
        movf    cmd_buf+4,W
        addlw   D'256' - '0'
        subwf   pin_stored2,W
        bnz     Cmd_PIN_Fail
        movf    cmd_buf+5,W
        addlw   D'256' - '0'
        subwf   pin_stored3,W
        bnz     Cmd_PIN_Fail

        movlw   D'1'
        movwf   door_unlocked
        movlw   SERVO_UNLOCKED
        movwf   servo_pulse_us
        movlw   D'5'
        movwf   relock_timer
        movlw   MODE_HOME
        movwf   sys_mode
        clrf    alarm_armed
        clrf    alarm_triggered
        clrf    wrong_beep_timer
        bcf     LATC,0
        clrf    pin_attempts
        movlw   LOG_PIN_OK
        call    Log_Event

        movlw   '$'
        call    UART_TxByte
        movlw   'P'
        call    UART_TxByte
        movlw   'O'
        call    UART_TxByte
        movlw   'K'
        call    UART_TxByte
        call    UART_TxCRLF
        goto    Parse_Reset
Cmd_PIN_Fail:
        incf    pin_attempts,F
        movlw   LOG_PIN_FAIL
        call    Log_Event
        movlw   '$'
        call    UART_TxByte
        movlw   'P'
        call    UART_TxByte
        movlw   'N'
        call    UART_TxByte
        movlw   'O'
        call    UART_TxByte
        call    UART_TxCRLF

        movlw   D'3'
        cpfslt  pin_attempts
        call    PIN_TooMany
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_LED  :  $Lnnn  manual LED brightness
;------------------------------------------------------------------------------
Cmd_LED:

        movf    cmd_buf+2,W
        addlw   D'256' - '0'
        mullw   D'100'
        movff   PRODL, led_tmp
        movf    cmd_buf+3,W
        addlw   D'256' - '0'
        mullw   D'10'
        movf    PRODL,W
        addwf   led_tmp,F
        movf    cmd_buf+4,W
        addlw   D'256' - '0'
        addwf   led_tmp,F

        movlw   D'100'
        cpfsgt  led_tmp
        bra     Cmd_LED_Set
        movwf   led_tmp
Cmd_LED_Set:
        movlw   D'1'
        movwf   led_override
        movf    led_tmp,W
        movwf   led_override_duty
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_AutoMode  :  $X  clear LED / fan overrides
;------------------------------------------------------------------------------
Cmd_AutoMode:

        clrf    led_override
        clrf    fan_override
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_Ctrl  :  $YA / $YM / $YV  control-mode select
;------------------------------------------------------------------------------
Cmd_Ctrl:

        movf    cmd_buf+2,W
        sublw   'A'
        bnz     CC_NotA
        clrf    ctrl_mode
        clrf    led_override
        clrf    fan_override
        goto    Parse_Reset
CC_NotA:
        movf    cmd_buf+2,W
        sublw   'M'
        bnz     CC_NotM
        movlw   D'1'
        movwf   ctrl_mode
        movlw   D'1'
        movwf   led_override
        movwf   fan_override
        movf    pwm_duty,W
        movwf   led_override_duty
        goto    Parse_Reset
CC_NotM:
        movf    cmd_buf+2,W
        sublw   'V'
        bnz     CC_Done
        movlw   D'2'
        movwf   ctrl_mode
        movlw   D'1'
        movwf   led_override
        movwf   fan_override
        movf    pwm_duty,W
        movwf   led_override_duty
CC_Done:
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_Fan  :  $F0 / $F1  fan relay control
;------------------------------------------------------------------------------
Cmd_Fan:
        movf    cmd_buf+2,W
        sublw   '1'
        bz      Cmd_FanOn
        bcf     LATC,2
        clrf    fan_on
        movlw   D'1'
        movwf   fan_override
        goto    Parse_Reset
Cmd_FanOn:
        bsf     LATC,2
        movlw   D'1'
        movwf   fan_on
        movwf   fan_override
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Cmd_Report  :  $R  dump activity log
;------------------------------------------------------------------------------
Cmd_Report:

        call    Log_Report
        goto    Parse_Reset

;------------------------------------------------------------------------------
; Parse_Reset  :  clear the command buffer
;------------------------------------------------------------------------------
Parse_Reset:
        clrf    cmd_idx
        return

;==============================================================================
; EEPROM  (PIN + ACTIVITY LOG)
;==============================================================================

;------------------------------------------------------------------------------
; EE_Read  :  read EEPROM byte (W = addr) -> W
;------------------------------------------------------------------------------
EE_Read:
        movwf   EEADR
        bcf     EECON1,EEPGD
        bcf     EECON1,CFGS
        bsf     EECON1,RD
        movf    EEDATA,W
        return

;------------------------------------------------------------------------------
; EE_Write  :  write ee_data to ee_addr (unlock sequence)
;------------------------------------------------------------------------------
EE_Write:
        movf    ee_addr,W
        movwf   EEADR
        movf    ee_data,W
        movwf   EEDATA
        bcf     EECON1,EEPGD
        bcf     EECON1,CFGS
        bsf     EECON1,WREN
        bcf     INTCON,GIEH
        bcf     INTCON,GIEL
        movlw   H'55'
        movwf   EECON2
        movlw   H'AA'
        movwf   EECON2
        bsf     EECON1,WR
        bsf     INTCON,GIEH
        bsf     INTCON,GIEL
EE_Write_Wait:
        btfsc   EECON1,WR
        bra     EE_Write_Wait
        bcf     EECON1,WREN
        return

;------------------------------------------------------------------------------
; EE_LoadPIN  :  load stored PIN; write default 1234 on first boot
;------------------------------------------------------------------------------
EE_LoadPIN:

        movlw   H'00'
        call    EE_Read
        sublw   H'A5'
        bz      EE_PIN_Exists

        movlw   H'01'
        movwf   ee_addr
        movlw   D'1'
        movwf   ee_data
        call    EE_Write
        movlw   H'02'
        movwf   ee_addr
        movlw   D'2'
        movwf   ee_data
        call    EE_Write
        movlw   H'03'
        movwf   ee_addr
        movlw   D'3'
        movwf   ee_data
        call    EE_Write
        movlw   H'04'
        movwf   ee_addr
        movlw   D'4'
        movwf   ee_data
        call    EE_Write

        movlw   H'00'
        movwf   ee_addr
        movlw   H'A5'
        movwf   ee_data
        call    EE_Write
EE_PIN_Exists:

        movlw   H'01'
        call    EE_Read
        movwf   pin_stored0
        movlw   H'02'
        call    EE_Read
        movwf   pin_stored1
        movlw   H'03'
        call    EE_Read
        movwf   pin_stored2
        movlw   H'04'
        call    EE_Read
        movwf   pin_stored3
        return

;------------------------------------------------------------------------------
; Log_Event  :  append event (W) to circular EEPROM log
;------------------------------------------------------------------------------
Log_Event:
        movwf   log_event_tmp

        movf    log_head,W
        addlw   H'10'
        movwf   ee_addr
        movf    log_event_tmp,W
        movwf   ee_data
        call    EE_Write

        incf    log_head,F
        movlw   D'8'
        cpfslt  log_head
        clrf    log_head

        movlw   H'18'
        movwf   ee_addr
        movf    log_head,W
        movwf   ee_data
        call    EE_Write
        return

;------------------------------------------------------------------------------
; Log_Report  :  transmit activity log over UART
;------------------------------------------------------------------------------
Log_Report:
        movlw   '$'
        call    UART_TxByte
        movlw   'R'
        call    UART_TxByte
        movlw   ','
        call    UART_TxByte
        movf    log_head,W
        call    UART_TxNum8

        movlw   H'10'
        movwf   ee_addr
LR_Loop:
        movlw   ','
        call    UART_TxByte
        movf    ee_addr,W
        call    EE_Read
        call    UART_TxNum8
        incf    ee_addr,F
        movlw   H'18'
        cpfseq  ee_addr
        bra     LR_Loop
        call    UART_TxCRLF
        return

;------------------------------------------------------------------------------
; Log_Init  :  restore log pointer, record boot event
;------------------------------------------------------------------------------
Log_Init:

        movlw   H'18'
        call    EE_Read
        movwf   log_head
        movlw   D'8'
        cpfslt  log_head
        clrf    log_head

        movlw   LOG_BOOT
        call    Log_Event
        return

;==============================================================================
; KEYPAD & PIN LOCK
;==============================================================================

;------------------------------------------------------------------------------
; Keypad_Scan  :  read SW1-SW4, debounce, set key_pressed
;------------------------------------------------------------------------------
Keypad_Scan:
        clrf    key_now

        btfss   PORTB,4
        bsf     key_now,0
        btfss   PORTB,5
        bsf     key_now,1
        btfss   PORTB,6
        bsf     key_now,2
        btfss   PORTB,7
        bsf     key_now,3

        comf    key_last,W
        andwf   key_now,W
        movwf   key_new

        movff   key_now, key_last

        clrf    key_pressed
        btfsc   key_new,0
        bra     KP_S1
        btfsc   key_new,1
        bra     KP_S2
        btfsc   key_new,2
        bra     KP_S3
        btfsc   key_new,3
        bra     KP_S4
        return
KP_S1:  movlw   D'1'
        movwf   key_pressed
        return
KP_S2:  movlw   D'2'
        movwf   key_pressed
        return
KP_S3:  movlw   D'3'
        movwf   key_pressed
        return
KP_S4:  movlw   D'4'
        movwf   key_pressed
        return

;------------------------------------------------------------------------------
; PIN_Handler  :  keypad PIN-entry state machine
;------------------------------------------------------------------------------
PIN_Handler:
        movf    key_pressed,W
        bz      PIN_Done

        movf    pin_active_timer,F
        bnz     PIN_AlreadyActive
        call    LCD_Clear
PIN_AlreadyActive:

        movlw   D'8'
        movwf   pin_active_timer

        movf    key_pressed,W
        sublw   D'1'
        bz      PIN_Inc
        movf    key_pressed,W
        sublw   D'2'
        bz      PIN_Next
        movf    key_pressed,W
        sublw   D'3'
        bz      PIN_Enter
        movf    key_pressed,W
        sublw   D'4'
        bz      PIN_Clear
        goto     PIN_Done

PIN_Inc:

        lfsr    FSR0, pin_entry0
        movf    pin_pos,W
        addwf   FSR0L,F
        incf    INDF0,F
        movlw   D'10'
        cpfslt  INDF0
        clrf    INDF0
        call    PIN_Display
        goto     PIN_Done

PIN_Next:
        incf    pin_pos,F
        movlw   D'4'
        cpfslt  pin_pos
        decf    pin_pos,F
        call    PIN_Display
        goto     PIN_Done

PIN_Clear:
        clrf    pin_entry0
        clrf    pin_entry1
        clrf    pin_entry2
        clrf    pin_entry3
        clrf    pin_pos
        call    PIN_Display
        goto     PIN_Done

PIN_Enter:

        movf    pin_entry0,W
        subwf   pin_stored0,W
        bnz     PIN_Wrong
        movf    pin_entry1,W
        subwf   pin_stored1,W
        bnz     PIN_Wrong
        movf    pin_entry2,W
        subwf   pin_stored2,W
        bnz     PIN_Wrong
        movf    pin_entry3,W
        subwf   pin_stored3,W
        bnz     PIN_Wrong

        movlw   D'1'
        movwf   door_unlocked
        movlw   SERVO_UNLOCKED
        movwf   servo_pulse_us
        movlw   D'5'
        movwf   relock_timer
        movlw   MODE_HOME
        movwf   sys_mode
        clrf    alarm_armed
        clrf    alarm_triggered
        clrf    wrong_beep_timer
        bcf     LATC,0
        clrf    pin_attempts
        call    LCD_CursorOff
        call    PIN_ShowGranted
        movlw   LOG_PIN_OK
        call    Log_Event

        clrf    pin_entry0
        clrf    pin_entry1
        clrf    pin_entry2
        clrf    pin_entry3
        clrf    pin_pos
        movlw   D'2'
        movwf   pin_active_timer
        goto     PIN_Done

PIN_Wrong:
        incf    pin_attempts,F
        bsf     LATC,0
        movlw   D'100'
        movwf   wrong_beep_timer
        movlw   LOG_PIN_FAIL
        call    Log_Event
        call    LCD_CursorOff
        call    PIN_ShowWrong
        movlw   D'2'
        movwf   pin_active_timer
        clrf    pin_entry0
        clrf    pin_entry1
        clrf    pin_entry2
        clrf    pin_entry3
        clrf    pin_pos

        movlw   D'3'
        cpfslt  pin_attempts
        call    PIN_TooMany

PIN_Done:
        return

;------------------------------------------------------------------------------
; PIN_TooMany  :  lockdown response after 3 failed attempts
;------------------------------------------------------------------------------
PIN_TooMany:
        movlw   MODE_AWAY
        movwf   sys_mode
        movlw   D'1'
        movwf   alarm_armed
        movlw   D'1'
        movwf   alarm_triggered
        clrf    door_unlocked
        movlw   SERVO_LOCKED
        movwf   servo_pulse_us
        clrf    pin_attempts
        movlw   LOG_INTRUSION
        call    Log_Event

        movlw   '!'
        call    UART_TxByte
        movlw   'I'
        call    UART_TxByte
        movlw   'N'
        call    UART_TxByte
        movlw   'T'
        call    UART_TxByte
        movlw   'R'
        call    UART_TxByte
        movlw   'U'
        call    UART_TxByte
        movlw   'D'
        call    UART_TxByte
        movlw   'E'
        call    UART_TxByte
        call    UART_TxCRLF
        return

;------------------------------------------------------------------------------
; Draw_Status_Labels  :  redraw static line-2 labels
;------------------------------------------------------------------------------
Draw_Status_Labels:
        movlw   H'40'
        call    LCD_Goto
        movlw   'L'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char
        movlw   H'46'
        call    LCD_Goto
        movlw   'I'
        call    LCD_Char
        movlw   'R'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char
        movlw   H'4B'
        call    LCD_Goto
        movlw   'S'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char
        return

;------------------------------------------------------------------------------
; PIN_Display  :  show current PIN entry on the LCD
;------------------------------------------------------------------------------
PIN_Display:

        movlw   H'00'
        call    LCD_Goto
        movlw   'E'
        call    LCD_Char
        movlw   'n'
        call    LCD_Char
        movlw   't'
        call    LCD_Char
        movlw   'e'
        call    LCD_Char
        movlw   'r'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   'P'
        call    LCD_Char
        movlw   'I'
        call    LCD_Char
        movlw   'N'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char

        movlw   H'40'
        call    LCD_Goto
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movf    pin_entry0,W
        addlw   '0'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movf    pin_entry1,W
        addlw   '0'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movf    pin_entry2,W
        addlw   '0'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movf    pin_entry3,W
        addlw   '0'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char

        movf    pin_pos,W
        mullw   D'2'
        movf    PRODL,W
        addlw   H'44'
        call    LCD_Goto
        call    LCD_CursorOn
        return

;------------------------------------------------------------------------------
; PIN_ShowGranted  :  show "ACCESS OK"
;------------------------------------------------------------------------------
PIN_ShowGranted:
        movlw   H'00'
        call    LCD_Goto
        movlw   'A'
        call    LCD_Char
        movlw   'C'
        call    LCD_Char
        movlw   'C'
        call    LCD_Char
        movlw   'E'
        call    LCD_Char
        movlw   'S'
        call    LCD_Char
        movlw   'S'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   'O'
        call    LCD_Char
        movlw   'K'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        return

;------------------------------------------------------------------------------
; PIN_ShowWrong  :  show "WRONG PIN"
;------------------------------------------------------------------------------
PIN_ShowWrong:
        movlw   H'00'
        call    LCD_Goto
        movlw   'W'
        call    LCD_Char
        movlw   'R'
        call    LCD_Char
        movlw   'O'
        call    LCD_Char
        movlw   'N'
        call    LCD_Char
        movlw   'G'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   'P'
        call    LCD_Char
        movlw   'I'
        call    LCD_Char
        movlw   'N'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        return

;==============================================================================
; SMART-HOME LOGIC ENGINE
;==============================================================================

;------------------------------------------------------------------------------
; Smart_Logic  :  per-second automation (mode dependent)
;------------------------------------------------------------------------------
Smart_Logic:

        movf    relock_timer,F
        bz      SL_NoRelock
        decf    relock_timer,F
        bnz     SL_NoRelock
        clrf    door_unlocked
        movlw   SERVO_LOCKED
        movwf   servo_pulse_us
SL_NoRelock:

        movf    ctrl_mode,F
        bnz     SL_Done

        movf    sys_mode,W
        bz      SL_Home
        goto    SL_Away

SL_Home:
        clrf    alarm_armed
        clrf    alarm_triggered
        clrf    wrong_beep_timer
        bcf     LATC,0

        call    Auto_Fan

        clrf    led_override
        goto     SL_Done

SL_Away:
        movlw   D'1'
        movwf   alarm_armed

        bcf     LATC,2
        clrf    fan_on

        decfsz  presence_timer,F
        goto     SL_AwayLED

        movf    sys_tick_lo,W
        andlw   B'00000111'
        addlw   D'4'
        movwf   presence_timer

        movlw   D'1'
        xorwf   presence_state,F
SL_AwayLED:

        movlw   D'1'
        movwf   led_override
        movf    presence_state,F
        bz      SL_AwayLEDoff
        movlw   D'80'
        movwf   led_override_duty
        goto     SL_Done
SL_AwayLEDoff:
        clrf    led_override_duty
        goto     SL_Done

SL_Done:
        return

;------------------------------------------------------------------------------
; Auto_Fan  :  temp / humidity fan control with hysteresis
;------------------------------------------------------------------------------
Auto_Fan:
        movf    fan_override,F
        bnz     AF_Done

        movf    temp_x10,W
        sublw   D'31'
        bnc     AF_Hot

        movf    temp_x10,W
        sublw   D'27'
        bc      AF_CheckCool
        movf    hum_pct,W
        sublw   D'69'
        bnc     AF_Hot

AF_CheckCool:

        movf    temp_x10,W
        sublw   D'30'
        bnc     AF_Done
        movf    hum_pct,W
        sublw   D'65'
        bnc     AF_Done
        bra     AF_Cool

AF_Hot:
        bsf     LATC,2
        movlw   D'1'
        movwf   fan_on
        bra     AF_Done
AF_Cool:
        bcf     LATC,2
        clrf    fan_on
AF_Done:
        return

;==============================================================================
; UART (EUSART) DRIVER
;==============================================================================

;------------------------------------------------------------------------------
; Init_UART  :  configure EUSART 9600 8N1, RX interrupt
;------------------------------------------------------------------------------
Init_UART:

        bsf     TRISC,7
        bsf     TRISC,6

        movlw   D'25'
        movwf   SPBRG

        movlw   B'00100100'
        movwf   TXSTA

        movlw   B'10010000'
        movwf   RCSTA

        bcf     PIR1,RCIF
        bcf     RCSTA,OERR
        bsf     RCSTA,CREN

        bcf     IPR1,RCIP
        bsf     PIE1,RCIE
        return

;------------------------------------------------------------------------------
; UART_TxByte  :  transmit one byte (W), blocking
;------------------------------------------------------------------------------
UART_TxByte:
        btfss   TXSTA,TRMT
        bra     UART_TxByte
        movwf   TXREG
        return

;------------------------------------------------------------------------------
; UART_TxCRLF  :  transmit CR + LF
;------------------------------------------------------------------------------
UART_TxCRLF:
        movlw   D'13'
        call    UART_TxByte
        movlw   D'10'
        call    UART_TxByte
        return

;------------------------------------------------------------------------------
; UART_TxNum8  :  transmit W as decimal (1-3 digits)
;------------------------------------------------------------------------------
UART_TxNum8:
        movwf   num_val
        clrf    lcd_num_d100
        clrf    lcd_num_d10
UT8_H:  movlw   D'100'
        cpfslt  num_val
        bra     UT8_SubH
        bra     UT8_AfterH
UT8_SubH:
        movlw   D'100'
        subwf   num_val,F
        incf    lcd_num_d100,F
        bra     UT8_H
UT8_AfterH:
UT8_T:  movlw   D'10'
        cpfslt  num_val
        bra     UT8_SubT
        bra     UT8_AfterT
UT8_SubT:
        movlw   D'10'
        subwf   num_val,F
        incf    lcd_num_d10,F
        bra     UT8_T
UT8_AfterT:
        movf    lcd_num_d100,F
        bz      UT8_NoH
        movf    lcd_num_d100,W
        addlw   '0'
        call    UART_TxByte
        movf    lcd_num_d10,W
        addlw   '0'
        call    UART_TxByte
        movf    num_val,W
        addlw   '0'
        call    UART_TxByte
        return
UT8_NoH:
        movf    lcd_num_d10,F
        bz      UT8_NoT
        movf    lcd_num_d10,W
        addlw   '0'
        call    UART_TxByte
UT8_NoT:
        movf    num_val,W
        addlw   '0'
        call    UART_TxByte
        return

;==============================================================================
; DHT11 TEMPERATURE / HUMIDITY
;==============================================================================

;------------------------------------------------------------------------------
; DHT_Read  :  read DHT11 -> hum_pct, temp_x10, dht_ok
;------------------------------------------------------------------------------
DHT_Read:
        clrf    dht_ok

        bcf     TRISB,0
        bcf     LATB,0

        bcf     INTCON,GIEH
        bcf     INTCON,GIEL
        call    DHT_Delay_18ms

        bsf     TRISB,0

        movlw   D'100'
        movwf   dht_timeout
DHT_WaitAckLo:
        decfsz  dht_timeout,F
        bra     DHT_WaitAckLo_chk
        bra     DHT_Fail
DHT_WaitAckLo_chk:
        btfsc   PORTB,0
        bra     DHT_WaitAckLo

        movlw   D'100'
        movwf   dht_timeout
DHT_WaitAckHi:
        decfsz  dht_timeout,F
        bra     DHT_WaitAckHi_chk
        bra     DHT_Fail
DHT_WaitAckHi_chk:
        btfss   PORTB,0
        bra     DHT_WaitAckHi

        movlw   D'100'
        movwf   dht_timeout
DHT_WaitFirstBit:
        decfsz  dht_timeout,F
        bra     DHT_WaitFirstBit_chk
        bra     DHT_Fail
DHT_WaitFirstBit_chk:
        btfsc   PORTB,0
        bra     DHT_WaitFirstBit

        lfsr    FSR0, dht_b0
        movlw   D'5'
        movwf   dht_byte_cnt

DHT_NextByte:
        clrf    INDF0
        movlw   D'8'
        movwf   dht_bit_cnt

DHT_NextBit:

        movlw   D'100'
        movwf   dht_timeout
DHT_WaitBitHi:
        decfsz  dht_timeout,F
        bra     DHT_WaitBitHi_chk
        bra     DHT_Fail
DHT_WaitBitHi_chk:
        btfss   PORTB,0
        bra     DHT_WaitBitHi

        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop

        rlncf   INDF0,F
        btfsc   PORTB,0
        bsf     INDF0,0

        movlw   D'100'
        movwf   dht_timeout
DHT_WaitBitEnd:
        btfss   PORTB,0
        bra     DHT_BitDone
        decfsz  dht_timeout,F
        bra     DHT_WaitBitEnd

DHT_BitDone:
        decfsz  dht_bit_cnt,F
        bra     DHT_NextBit

        movf    POSTINC0,W
        decfsz  dht_byte_cnt,F
        bra     DHT_NextByte

        movf    dht_b0,W
        addwf   dht_b1,W
        addwf   dht_b2,W
        addwf   dht_b3,W

        subwf   dht_b4,W
        bnz     DHT_Fail

        movff   dht_b0, hum_pct
        movff   dht_b2, temp_x10
        movlw   D'1'
        movwf   dht_ok

DHT_Fail:

        bsf     INTCON,GIEH
        bsf     INTCON,GIEL
        return

;------------------------------------------------------------------------------
; DHT_Delay_18ms  :  18 ms start-signal delay
;------------------------------------------------------------------------------
DHT_Delay_18ms:
        movlw   D'30'
        movwf   dht_d_outer
DHT_D_OL:
        movlw   D'200'
        movwf   dht_d_inner
DHT_D_IL:
        nop
        decfsz  dht_d_inner,F
        bra     DHT_D_IL
        decfsz  dht_d_outer,F
        bra     DHT_D_OL
        return

;==============================================================================
; TIMER 2 / INTERRUPT SETUP
;==============================================================================

;------------------------------------------------------------------------------
; Init_Timer2  :  1 ms tick for software PWM, low priority
;------------------------------------------------------------------------------
Init_Timer2:
        clrf    T2CON
        movlw   D'99'
        movwf   PR2
        bcf     PIR1,TMR2IF
        bcf     IPR1,TMR2IP
        bsf     PIE1,TMR2IE
        bsf     T2CON,TMR2ON
        return

;------------------------------------------------------------------------------
; Init_Interrupts  :  enable prioritised interrupts
;------------------------------------------------------------------------------
Init_Interrupts:
        bsf     RCON,IPEN
        bsf     INTCON,GIEH
        bsf     INTCON,GIEL
        return

;==============================================================================
; INTERRUPT SERVICE ROUTINES
;==============================================================================

;------------------------------------------------------------------------------
; ISR_High  :  Timer0 tick + servo pulse generation
;------------------------------------------------------------------------------
ISR_High:
        btfss   INTCON,TMR0IF
        bra     ISR_High_Exit
        bcf     INTCON,TMR0IF

        movlw   H'F6'
        movwf   TMR0H
        movlw   H'3C'
        movwf   TMR0L

        incf    sys_tick_lo,F

        decfsz  tick_100ms_cnt,F
        bra     ISR_High_Exit
        movlw   D'10'
        movwf   tick_100ms_cnt
        bsf     sys_flags,SF_T100MS

        decfsz  tick_1s_cnt,F
        bra     Servo_Pulse_Check
        movlw   D'10'
        movwf   tick_1s_cnt
        bsf     sys_flags,SF_T1S

Servo_Pulse_Check:

        decfsz  servo_frame_cnt,F
        bra     ISR_High_Exit
        movlw   D'2'
        movwf   servo_frame_cnt

        movf    servo_pulse_us,W
        movwf   servo_dl_lo
        bsf     LATC,1
ServoHi:
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        decfsz  servo_dl_lo,F
        bra     ServoHi
        bcf     LATC,1

ISR_High_Exit:
        retfie  FAST

;------------------------------------------------------------------------------
; ISR_Low  :  UART RX + Timer2 software PWM
;------------------------------------------------------------------------------
ISR_Low:
        movwf   isr_w_save
        movff   STATUS, isr_status_save
        movff   BSR,    isr_bsr_save

        btfss   PIR1,RCIF
        bra     ISR_Low_NoRx

        btfsc   RCSTA,OERR
        bcf     RCSTA,CREN
        btfsc   RCSTA,OERR
        bsf     RCSTA,CREN

        movf    RCREG,W
        movwf   rx_byte
        bsf     sys_flags,SF_RX_READY

ISR_Low_NoRx:

        btfss   PIR1,TMR2IF
        bra     ISR_Low_Exit
        bcf     PIR1,TMR2IF

        incf    pwm_cnt,F
        movlw   D'100'
        cpfslt  pwm_cnt
        clrf    pwm_cnt

        movf    pwm_duty,W
        cpfslt  pwm_cnt
        bra     PWM_Off
        bsf     LATA,4
        bra     ISR_Low_Exit
PWM_Off:
        bcf     LATA,4

ISR_Low_Exit:
        movff   isr_bsr_save, BSR
        movf    isr_w_save, W
        movff   isr_status_save, STATUS
        retfie

;==============================================================================
; MAIN LOOP
;==============================================================================

;------------------------------------------------------------------------------
; Main_Loop  :  foreground scheduler (10 ms / 100 ms / 1 s)
;------------------------------------------------------------------------------
Main_Loop:

        btfss   sys_flags,SF_RX_READY
        goto     ML_NoRx
        bcf     sys_flags,SF_RX_READY

        movf    rx_byte,W
        call    UART_TxByte
        call    Parse_Char
ML_NoRx:

        btfss   sys_flags,SF_T10MS
        goto     ML_Skip10
        bcf     sys_flags,SF_T10MS

ML_Skip10:

        btfss   sys_flags,SF_T100MS
        goto     ML_Skip100
        bcf     sys_flags,SF_T100MS

        call    ADC_Read_AN1

        movf    pin_active_timer,F
        bnz     LCD_L2_Skip

        movlw   H'42'
        call    LCD_Goto
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   H'42'
        call    LCD_Goto
        movf    ldr_val,W
        call    LCD_Num8

        movlw   H'49'
        call    LCD_Goto
        movlw   'N'
        btfsc   PORTB,1
        movlw   'Y'
        call    LCD_Char

        movlw   H'4D'
        call    LCD_Goto
        movlw   'L'
        movf    door_unlocked,F
        bz      ServoLetterReady
        movlw   'U'
ServoLetterReady:
        call    LCD_Char
LCD_L2_Skip:

        btfss   PORTB,1
        goto     IR_IsIdle

        movlw   D'8'
        cpfslt  ir_count
        goto     IR_DoCheck
        incf    ir_count,F
        goto     IR_DoCheck
IR_IsIdle:

        movf    ir_count,F
        bz      IR_DoCheck
        decf    ir_count,F
IR_DoCheck:

        movf    sys_mode,F
        bnz     IR_NoWelcome
        movf    ctrl_mode,F
        bnz     IR_NoWelcome
        btfss   PORTB,1
        bra     IR_NoWelcome

        movlw   D'30'
        movwf   welcome_timer
IR_NoWelcome:

        movf    alarm_armed,F
        bnz     IR_Armed
        clrf    ir_count
        goto     PIR_Skip
IR_Armed:

        movlw   D'4'
        cpfslt  ir_count
        goto     PIR_DoTrig
        goto     PIR_Skip
PIR_DoTrig:
        movf    alarm_triggered,F
        bnz     PIR_Skip
        movlw   D'1'
        movwf   alarm_triggered

        clrf    door_unlocked
        movlw   SERVO_LOCKED
        movwf   servo_pulse_us
        movlw   LOG_INTRUSION
        call    Log_Event

        movlw   '!'
        call    UART_TxByte
        movlw   'I'
        call    UART_TxByte
        movlw   'N'
        call    UART_TxByte
        movlw   'T'
        call    UART_TxByte
        movlw   'R'
        call    UART_TxByte
        movlw   'U'
        call    UART_TxByte
        movlw   'D'
        call    UART_TxByte
        movlw   'E'
        call    UART_TxByte
        call    UART_TxCRLF
PIR_Skip:

        movf    alarm_triggered,F
        bz      Buzz_CheckWrong
        btg     LATC,0
        goto     Buzz_Done
Buzz_CheckWrong:

        movf    wrong_beep_timer,F
        bz      Buzz_Off
        decf    wrong_beep_timer,F
        bsf     LATC,0
        goto     Buzz_Done
Buzz_Off:
        bcf     LATC,0
Buzz_Done:

        call    Keypad_Scan
        call    PIN_Handler

        movf    led_override,F
        bz      PWM_AutoMode

        movf    led_override_duty,W
        movwf   pwm_duty
        goto     PWM_DoneSet2
PWM_AutoMode:

        movlw   D'75'
        cpfslt  ldr_val
        goto     PWM_Off_Bright

        movf    ldr_val,W
        sublw   D'75'

        bcf     STATUS,C
        rlncf   WREG,W
        rlncf   WREG,W
        movwf   pwm_duty

        movlw   D'100'
        cpfsgt  pwm_duty
        goto     PWM_DoneSet
        movwf   pwm_duty
        goto     PWM_DoneSet
PWM_Off_Bright:
        clrf    pwm_duty
PWM_DoneSet:

        movf    welcome_timer,F
        bz      PWM_DoneSet2
        movlw   D'80'
        cpfsgt  pwm_duty
        movwf   pwm_duty

        decf    welcome_timer,F
PWM_DoneSet2:

ML_Skip100:

        btfss   sys_flags,SF_T1S
        goto     ML_Skip1s
        bcf     sys_flags,SF_T1S

        btg     LATE,0
        incf    seconds_count,F

        call    Smart_Logic

        movf    pin_active_timer,F
        bz      PIN_Timer_Done
        decf    pin_active_timer,F
        bnz     PIN_Timer_Done

        clrf    pin_entry0
        clrf    pin_entry1
        clrf    pin_entry2
        clrf    pin_entry3
        clrf    pin_pos
        call    LCD_CursorOff
        call    LCD_Clear
        call    Draw_Status_Labels
        clrf    dht_ok
PIN_Timer_Done:

        decfsz  dht_interval,F
        goto     DHT_Skip
        movlw   D'2'
        movwf   dht_interval
        call    DHT_Read
DHT_Skip:

        movf    dht_ok,F
        bz      EnvAlert_Skip
        movf    hot_alert_sent,F
        bnz     EA_HotCheckClear

        movlw   D'39'
        cpfsgt  temp_x10
        bra     EA_NotHot
        movlw   D'1'
        movwf   hot_alert_sent
        movlw   LOG_HOTALERT
        call    Log_Event

        movlw   '!'
        call    UART_TxByte
        movlw   'H'
        call    UART_TxByte
        movlw   'O'
        call    UART_TxByte
        movlw   'T'
        call    UART_TxByte
        movlw   'A'
        call    UART_TxByte
        movlw   'L'
        call    UART_TxByte
        movlw   'R'
        call    UART_TxByte
        movlw   'T'
        call    UART_TxByte
        call    UART_TxCRLF
        bra     EA_NotHot
EA_HotCheckClear:

        movlw   D'38'
        cpfslt  temp_x10
        bra     EA_NotHot
        clrf    hot_alert_sent
EA_NotHot:

        movf    hum_alert_sent,F
        bnz     EA_HumCheckClear
        movlw   D'89'
        cpfsgt  hum_pct
        bra     EnvAlert_Skip
        movlw   D'1'
        movwf   hum_alert_sent
        movlw   LOG_HUMALERT
        call    Log_Event
        movlw   '!'
        call    UART_TxByte
        movlw   'H'
        call    UART_TxByte
        movlw   'U'
        call    UART_TxByte
        movlw   'M'
        call    UART_TxByte
        movlw   'A'
        call    UART_TxByte
        movlw   'L'
        call    UART_TxByte
        movlw   'R'
        call    UART_TxByte
        movlw   'T'
        call    UART_TxByte
        call    UART_TxCRLF
        bra     EnvAlert_Skip
EA_HumCheckClear:
        movlw   D'85'
        cpfslt  hum_pct
        bra     EnvAlert_Skip
        clrf    hum_alert_sent
EnvAlert_Skip:

        movf    pin_active_timer,F
        bnz     LCD_Line1_Skip
        movf    dht_ok,F
        bz      LCD_Line1_Skip
        movlw   H'00'
        call    LCD_Goto
        movlw   'T'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char
        movf    temp_x10,W
        call    LCD_Num8
        movlw   'C'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char
        movlw   'H'
        call    LCD_Char
        movlw   ':'
        call    LCD_Char
        movf    hum_pct,W
        call    LCD_Num8
        movlw   '%'
        call    LCD_Char
        movlw   ' '
        call    LCD_Char

        movlw   H'0D'
        call    LCD_Goto
        movlw   '['
        call    LCD_Char
        movf    sys_mode,W
        bz      L1_ModeH
        movlw   'A'
        goto     L1_ModePrint
L1_ModeH:
        movlw   'H'
L1_ModePrint:
        call    LCD_Char
        movlw   ']'
        call    LCD_Char
LCD_Line1_Skip:

        movlw   '$'
        call    UART_TxByte
        movlw   'S'
        call    UART_TxByte
        movlw   ','
        call    UART_TxByte
        movf    ldr_val,W
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte
        movf    temp_x10,W
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte
        movf    hum_pct,W
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte
        movf    sys_mode,W
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte
        movf    door_unlocked,W
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte
        movf    fan_on,W
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte

        movlw   D'0'
        btfsc   PORTB,1
        movlw   D'1'
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte

        movf    pwm_duty,W
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte

        movf    ctrl_mode,W
        call    UART_TxNum8
        movlw   ','
        call    UART_TxByte

        movf    alarm_armed,W
        call    UART_TxNum8
        call    UART_TxCRLF

ML_Skip1s:
        bra     Main_Loop

;==============================================================================
; DISPLAY UTILITIES
;==============================================================================

;------------------------------------------------------------------------------
; Hex_Digit_To_LCD  :  print W (0-15) as a hex character
;------------------------------------------------------------------------------
Hex_Digit_To_LCD:
        movwf   lcd_arg
        movlw   D'10'
        cpfslt  lcd_arg
        bra     HD_AF
        movf    lcd_arg,W
        addlw   '0'
        call    LCD_Char
        return
HD_AF:
        movf    lcd_arg,W
        addlw   '7'
        call    LCD_Char
        return

;==============================================================================
; LCD DRIVER
;==============================================================================

;------------------------------------------------------------------------------
; Delay_50us  :  blocking 50 us delay
;------------------------------------------------------------------------------
Delay_50us:
        movlw   D'17'
        movwf   d50_cnt
D50_Loop:
        decfsz  d50_cnt,F
        bra     D50_Loop
        return

;------------------------------------------------------------------------------
; Delay_2ms  :  blocking 2 ms delay
;------------------------------------------------------------------------------
Delay_2ms:
        movlw   D'4'
        movwf   d2ms_outer
D2_OL:
        movlw   D'250'
        movwf   d2ms_inner
D2_IL:
        nop
        decfsz  d2ms_inner,F
        bra     D2_IL
        decfsz  d2ms_outer,F
        bra     D2_OL
        return

;------------------------------------------------------------------------------
; Delay_20ms  :  blocking 20 ms delay
;------------------------------------------------------------------------------
Delay_20ms:
        movlw   D'10'
        movwf   d20ms_cnt
D20_Loop:
        call    Delay_2ms
        decfsz  d20ms_cnt,F
        bra     D20_Loop
        return

;------------------------------------------------------------------------------
; LCD_Pulse_EN  :  strobe the LCD enable line
;------------------------------------------------------------------------------
LCD_Pulse_EN:
        bsf     LATD,5
        nop
        nop
        bcf     LATD,5
        return

;------------------------------------------------------------------------------
; LCD_Write_Nibble  :  write low nibble of W to the data lines
;------------------------------------------------------------------------------
LCD_Write_Nibble:
        andlw   B'00001111'
        movwf   lcd_tmp
        movf    LATD,W
        andlw   B'11110000'
        iorwf   lcd_tmp,W
        movwf   LATD
        call    LCD_Pulse_EN
        return

;------------------------------------------------------------------------------
; LCD_Cmd  :  send command byte (W) to the LCD
;------------------------------------------------------------------------------
LCD_Cmd:
        movwf   lcd_arg
        bcf     LATD,4
        swapf   lcd_arg,W
        call    LCD_Write_Nibble
        movf    lcd_arg,W
        call    LCD_Write_Nibble
        call    Delay_50us
        return

;------------------------------------------------------------------------------
; LCD_Cmd_Slow  :  send command + 2 ms settle
;------------------------------------------------------------------------------
LCD_Cmd_Slow:
        call    LCD_Cmd
        call    Delay_2ms
        return

;------------------------------------------------------------------------------
; LCD_Char  :  write character (W) to the LCD
;------------------------------------------------------------------------------
LCD_Char:
        movwf   lcd_arg
        bsf     LATD,4
        swapf   lcd_arg,W
        call    LCD_Write_Nibble
        movf    lcd_arg,W
        call    LCD_Write_Nibble
        call    Delay_50us
        return

;------------------------------------------------------------------------------
; LCD_Init  :  initialise LCD in 4-bit mode
;------------------------------------------------------------------------------
LCD_Init:
        bcf     LATD,4
        bcf     LATD,5
        call    Delay_20ms
        movlw   B'00000011'
        call    LCD_Write_Nibble
        call    Delay_2ms
        movlw   B'00000011'
        call    LCD_Write_Nibble
        call    Delay_2ms
        movlw   B'00000011'
        call    LCD_Write_Nibble
        call    Delay_50us
        movlw   B'00000010'
        call    LCD_Write_Nibble
        call    Delay_50us
        movlw   B'00101000'
        call    LCD_Cmd
        movlw   B'00001000'
        call    LCD_Cmd
        movlw   B'00000001'
        call    LCD_Cmd_Slow
        movlw   B'00000110'
        call    LCD_Cmd
        movlw   B'00001100'
        call    LCD_Cmd
        return

;------------------------------------------------------------------------------
; LCD_Clear  :  clear the display
;------------------------------------------------------------------------------
LCD_Clear:
        movlw   B'00000001'
        call    LCD_Cmd_Slow
        return

;------------------------------------------------------------------------------
; LCD_Goto  :  set DDRAM address (W)
;------------------------------------------------------------------------------
LCD_Goto:
        iorlw   B'10000000'
        call    LCD_Cmd
        return

;------------------------------------------------------------------------------
; LCD_CursorOn  :  enable the blinking cursor
;------------------------------------------------------------------------------
LCD_CursorOn:
        movlw   B'00001111'
        call    LCD_Cmd
        return

;------------------------------------------------------------------------------
; LCD_CursorOff  :  disable the cursor
;------------------------------------------------------------------------------
LCD_CursorOff:
        movlw   B'00001100'
        call    LCD_Cmd
        return

;------------------------------------------------------------------------------
; LCD_Num8  :  print W as decimal (1-3 digits)
;------------------------------------------------------------------------------
LCD_Num8:

        movwf   num_val
        clrf    lcd_num_d100
        clrf    lcd_num_d10
N8_H:   movlw   D'100'
        cpfslt  num_val
        bra     N8_SubH
        bra     N8_AfterH
N8_SubH:
        movlw   D'100'
        subwf   num_val,F
        incf    lcd_num_d100,F
        bra     N8_H
N8_AfterH:
N8_T:   movlw   D'10'
        cpfslt  num_val
        bra     N8_SubT
        bra     N8_AfterT
N8_SubT:
        movlw   D'10'
        subwf   num_val,F
        incf    lcd_num_d10,F
        bra     N8_T
N8_AfterT:

        movf    lcd_num_d100,F
        bz      N8_NoH

        movf    lcd_num_d100,W
        addlw   '0'
        call    LCD_Char
        movf    lcd_num_d10,W
        addlw   '0'
        call    LCD_Char
        movf    num_val,W
        addlw   '0'
        call    LCD_Char
        return
N8_NoH:
        movf    lcd_num_d10,F
        bz      N8_NoT
        movf    lcd_num_d10,W
        addlw   '0'
        call    LCD_Char
N8_NoT:
        movf    num_val,W
        addlw   '0'
        call    LCD_Char
        return

        END
