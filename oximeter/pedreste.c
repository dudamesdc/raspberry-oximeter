#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "ssd1306.h"

// --- Pinos e Configurações ---
#define LED_R_PIN 13
#define ONBOARD_LED_PIN LED_R_PIN // CORREÇÃO: Usa o LED vermelho como indicador de erro
#define BUZZER_PIN 21
#define LED_G_PIN 11
#define BTN_A_PIN 5
#define BTN_B_PIN 6

#define I2C_PORT i2c1
#define PINO_SCL 14
#define PINO_SDA 15

#define BUZZER_FREQUENCY 2000

// --- Variáveis Globais ---
ssd1306_t display;
uint8_t display_addr = 0;

// --- Máquina de Estados ---
typedef enum {
    STATE_IDLE,
    STATE_READING,
    STATE_ALARM,
    STATE_NORMAL
} app_state_t;

// --- Funções do Buzzer ---
void pwm_init_buzzer(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_config config = pwm_get_default_config();
    float div = (float)clock_get_hz(clk_sys) / (BUZZER_FREQUENCY * 4096);
    pwm_config_set_clkdiv(&config, div);
    pwm_config_set_wrap(&config, 4095);
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(pin, 0);
}

void beep() {
    pwm_set_gpio_level(BUZZER_PIN, 2048);
}

void stop_beep() {
    pwm_set_gpio_level(BUZZER_PIN, 0);
}

// --- Função de Diagnóstico ---
void signal_error_led() {
    // Esta função agora piscará o LED vermelho se o display não for encontrado.
    gpio_init(ONBOARD_LED_PIN);
    gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
    while(true) {
        gpio_put(ONBOARD_LED_PIN, 1);
        sleep_ms(200);
        gpio_put(ONBOARD_LED_PIN, 0);
        sleep_ms(200);
    }
}

// --- Função de Inicialização Completa ---
void setup_hardware() {
    stdio_init_all();
    sleep_ms(1000);
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(PINO_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PINO_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PINO_SDA);
    gpio_pull_up(PINO_SCL);

    uint8_t rxdata;
    if (i2c_read_blocking(I2C_PORT, 0x3C, &rxdata, 1, false) >= 0) {
        display_addr = 0x3C;
    } else if (i2c_read_blocking(I2C_PORT, 0x3D, &rxdata, 1, false) >= 0) {
        display_addr = 0x3D;
    }
    
    if (display_addr == 0) {
        // Se o display não for encontrado, chama a função de erro.
        signal_error_led();
    }

    display.external_vcc = false;
    ssd1306_init(&display, 128, 64, display_addr, I2C_PORT);
    ssd1306_clear(&display);
    ssd1306_show(&display);

    gpio_init(LED_R_PIN); gpio_set_dir(LED_R_PIN, GPIO_OUT);
    gpio_init(LED_G_PIN); gpio_set_dir(LED_G_PIN, GPIO_OUT);
    gpio_init(BTN_A_PIN); gpio_set_dir(BTN_A_PIN, GPIO_IN); gpio_pull_up(BTN_A_PIN);
    gpio_init(BTN_B_PIN); gpio_set_dir(BTN_B_PIN, GPIO_IN); gpio_pull_up(BTN_B_PIN);
    
    pwm_init_buzzer(BUZZER_PIN);
}

void update_oled(const char *line1, const char *line2) {
    ssd1306_clear(&display);
    if (line1) ssd1306_draw_string(&display, 8, 24, 2, (char*)line1);
    if (line2) ssd1306_draw_string(&display, 8, 40, 2, (char*)line2);
    ssd1306_show(&display);
}

void simulate_sensor_data(float *bpm, float *spo2) {
    *bpm = 70 + (rand() % 31);
    *spo2 = 96 + (rand() % 4);
    if (rand() % 4 == 0) {
        int alarm_type = rand() % 3;
        if (alarm_type == 0) *bpm = 50;
        if (alarm_type == 1) *bpm = 110;
        if (alarm_type == 2) *spo2 = 92;
    }
}

// --- Lógica Principal ---
int main() {
    setup_hardware();
    
    app_state_t current_state = STATE_IDLE;
    float bpm = 0.0f, spo2 = 0.0f;

    while (true) {
        switch (current_state) {
            case STATE_IDLE:
                gpio_put(LED_G_PIN, 0);
                gpio_put(LED_R_PIN, 0);
                stop_beep();
                update_oled("OXIMETRO", "Pressione A");
                if (!gpio_get(BTN_A_PIN)) {
                    sleep_ms(200);
                    current_state = STATE_READING;
                }
                break;

            case STATE_READING:
                update_oled("Lendo dados...", "");
                simulate_sensor_data(&bpm, &spo2);
                sleep_ms(2000);
                if ((bpm < 60 || bpm > 100) || (spo2 < 95)) {
                    current_state = STATE_ALARM;
                } else {
                    current_state = STATE_NORMAL;
                }
                break;

            case STATE_NORMAL:
                {
                    char bpm_str[16], spo2_str[16];
                    snprintf(bpm_str, sizeof(bpm_str), "BPM: %.0f", bpm);
                    snprintf(spo2_str, sizeof(spo2_str), "SpO2: %.0f%%", spo2);
                    update_oled(bpm_str, spo2_str);
                    gpio_put(LED_G_PIN, 1);
                    gpio_put(LED_R_PIN, 0);
                    stop_beep();
                    if (!gpio_get(BTN_B_PIN)) {
                        sleep_ms(200);
                        current_state = STATE_IDLE;
                    }
                }
                break;

            case STATE_ALARM:
                {
                    char bpm_str[16], spo2_str[16];
                    snprintf(bpm_str, sizeof(bpm_str), "BPM: %.0f", bpm);
                    snprintf(spo2_str, sizeof(spo2_str), "SpO2: %.0f%%", spo2);
                    update_oled(bpm_str, spo2_str);
                    gpio_put(LED_R_PIN, 1);
                    gpio_put(LED_G_PIN, 0);
                    beep();
                    if (!gpio_get(BTN_B_PIN)) {
                        sleep_ms(200);
                        current_state = STATE_IDLE;
                    }
                }
                break;
        }
        sleep_ms(10);
    }
    return 0;
}