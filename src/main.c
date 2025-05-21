#include "main.h"

#include "G_background_scr.h"
#include "font16x16.h"
#include "lua/lauxlib.h"
#include "lua/ldebug.h"
#include "lua/lua.h"
#include "lua/lualib.h"
#include "phonght32.h"
#include "pkklib.h"

int rand(void);

// #define LLONG_MAX 0x7FFFFFFFFFFFFFFF
// #define LLONG_MIN 0x8000000000000000

#define MAX_PROMPT_LEN 256

#define STACK_SIZE 100

typedef enum { VAL_RATIONAL, VAL_FLOAT } RPNValueType;

typedef struct {
    long long num;  // numerator
    long long den;  // denominator
} Rational;

typedef struct {
    RPNValueType type;
    union {
        Rational r;
        double f;
    };
} RPNValue;

typedef struct {
    char *name;
    char id, parent;
} Command;

Command cmds[] = {
    {"ang", 1, 0},  //  angle
    {"deg", 2, 1},  //  degree
    {"rad", 3, 1},  //  radian
    {"bas", 4, 0},  // base
    {"bin", 5, 4},  // binary
    {"dec", 6, 4},  // decimal
    {"hex", 7, 4},  // hexadecimal
};
int currentMenu = 0;
u16 menuF[4];

RPNValue stack[STACK_SIZE];
char prompt[128];

u8 showFract = 0;
u8 dispChanged = 0;
u8 degree = 1;
u8 base = 10;

void initLUA(void) {
    lua_State *program;

    char *initscript =
        "keyboard={}; keyboard['pressed']={}; keyboard['pressedcode']={};"
        "mouse={}; mouse['pressed']={};"
        "module={};";

    // fgb.s->epoch = 0;
    // if (previousState) {
    //     lua_close(previousState);
    // }

    program = luaL_newstate();
}

long long gcd(long long a, long long b) {
    while (b != 0) {
        long long t = b;
        b = a % b;
        a = t;
    }
    return a < 0 ? -a : a;
}

void simplify(Rational *r) {
    long long g = gcd(r->num, r->den);
    r->num /= g;
    r->den /= g;
    if (r->den < 0) {
        r->num = -r->num;
        r->den = -r->den;
    }
}

void pad_left(char *dest, const char *src, size_t width) {
    size_t len = strlen(src);
    if (len >= width) {
        strncpy(dest, src, width);
        dest[width - 2] = '.';
        dest[width - 1] = '.';
        dest[width] = 0;
    } else {
        size_t padding = width - len;
        memset(dest, ' ', padding);
        strcpy(dest + padding, src);
    }
}

void to_base(long long RPNValue, char *buffer, int base) {
    if (base < 2 || base > 36) {
        strcpy(buffer, "?");
        return;
    }

    const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i = 0;
    int is_negative = 0;

    if (RPNValue == 0) {
        strcpy(buffer, "0");
        return;
    }

    if (RPNValue < 0) {
        is_negative = 1;
        RPNValue = -RPNValue;
    }

    while (RPNValue > 0) {
        buffer[i++] = digits[RPNValue % base];
        RPNValue /= base;
    }

    if (is_negative) buffer[i++] = '-';

    buffer[i] = '\0';

    // Reverse
    for (int j = 0; j < i / 2; ++j) {
        char tmp = buffer[j];
        buffer[j] = buffer[i - j - 1];
        buffer[i - j - 1] = tmp;
    }
}

void cnvRPNValueToString(RPNValue *v, char *str) {
    char str2[MAX_PROMPT_LEN];

    if (v->type == VAL_FLOAT) {
        printf("%.8g\n", v->f);

        sprintf(str2, "%.8g", v->f);
        pad_left(str, str2, 21);
    } else {
        if (v->r.den == 1) {
            to_base(v->r.num, str2, base);
            pad_left(str, str2, 21);

            // sprintf(str, "%21lld", v->r.num);
        } else if (v->r.den == 0) {
            pad_left(str, "Inf", 21);
        } else {
            if (showFract) {
                sprintf(str2, "%lld/%lld", v->r.num, v->r.den);

                pad_left(str, str2, 21);
            } else {
                double f = (double)v->r.num / (double)v->r.den;
                sprintf(str, "%21.4f", f);
            }
        }
    }
}

// draw the stack if changed
void draw_stack(void) {
    static u8 blink = 0;
    uint16_t fg = 0xFFFF;
    uint16_t bg = RGB565(75, 105, 47);

    if (dispChanged) {
        char str[MAX_PROMPT_LEN];
        uint16_t n = 0;

        if (prompt[0] == 0) {
            for (n = 0; n < 8; n++) {
                sprintf(str, "%d:", n + 1);
                pkk_draw_text(phonght32, 0, 272 - n * 32, str, fg, bg);

                cnvRPNValueToString(&stack[n], str);
                pkk_draw_text(phonght32, 320 - strlen(str) * 14, 272 - n * 32,
                              str, fg, bg);
            }
        } else {
            for (n = 0; n < 7; n++) {
                sprintf(str, "%d:", n + 1);
                pkk_draw_text(phonght32, 0, 272 - n * 32 - 32, str, fg, bg);

                cnvRPNValueToString(&stack[n], str);
                pkk_draw_text(phonght32, 320 - strlen(str) * 14,
                              272 - n * 32 - 32, str, fg, bg);
            }

            sprintf(str, "%-22s", prompt);

            pkk_draw_rect_fill(0, 272, 12, 272 + 32, bg);
            pkk_draw_text(phonght32, 12, 272, str, fg, bg);
        }

        dispChanged = 0;
    }

    if (prompt[0] != 0) {
        blink++;
        if (blink > 16) {
            pkk_draw_text(phonght32, 12 + strlen(prompt) * 14, 272, "~", fg,
                          bg);
            if (blink > 32) {
                blink = 0;
            }
        } else {
            pkk_draw_text(phonght32, 12 + strlen(prompt) * 14, 272, " ", fg,
                          bg);
        }
    }
}

void removeStackAt(int n) {
    do {
        memcpy(&stack[n], &stack[n + 1], sizeof(RPNValue));
        n++;
    } while (n < STACK_SIZE - 1);
} /* removeStackAt */

/**
 * @brief Pushes the stack up by one position.
 *
 * This function shifts all elements in the stack up by one position,
 * making room at the bottom of the stack for a new RPNValue.
 */
void pushStack(void) {
    u8 n;
    // push the stack

    n = STACK_SIZE - 2;
    do {
        memcpy(&stack[n], &stack[n - 1], sizeof(RPNValue));
        n--;
    } while (n > 0);

} /* pushStack */

/**
 * @brief Adds the current prompt RPNValue to the stack.
 *
 * This function checks if the prompt buffer contains a RPNValue. If so, it
 * pushes a new element onto the stack.
 */
void addPromptToStack(void) {
    if (prompt[0] == 0) {
        return;
    }

    pushStack();
    // test if prompt contains a dot
    char *dot = strchr(prompt, '.');
    if (dot != NULL) {
        // Sépare la partie entière
        long long int_part = 0;
        char *p = prompt;
        int negative = 0;

        if (*p == '-') {
            negative = 1;
            p++;
        }

        while (p < dot) {
            if (*p >= '0' && *p <= '9') {
                int_part = int_part * 10 + (*p - '0');
            }
            p++;
        }

        // Convertit la partie décimale
        p = dot + 1;
        long long frac_part = 0;
        long long frac_len = 0;

        while (*p >= '0' && *p <= '9') {
            frac_part = frac_part * 10 + (*p - '0');
            frac_len++;
            p++;
        }

        long long denominator = 1;
        for (int i = 0; i < frac_len; i++) {
            denominator *= 10;
        }

        long long numerator = int_part * denominator + frac_part;
        if (negative) numerator = -numerator;

        stack[0].type = VAL_RATIONAL;
        stack[0].r.num = numerator;
        stack[0].r.den = denominator;
        simplify(&stack[0].r);
    } else {
        // convert to rational
        stack[0].type = VAL_RATIONAL;
        sscanf(prompt, "%lld", &stack[0].r.num);
        stack[0].r.den = 1;
    }

    prompt[0] = 0;
} /* addPromptToStack */

void cnvRationalToFloat(void) {
    if (stack[0].type == VAL_RATIONAL) {
        stack[0].f = (double)stack[0].r.num / (double)stack[0].r.den;
        stack[0].type = VAL_FLOAT;
    }
    if (stack[1].type == VAL_RATIONAL) {
        stack[1].f = (double)stack[1].r.num / (double)stack[1].r.den;
        stack[1].type = VAL_FLOAT;
    }
}

void doAddition(void) {
    if ((stack[0].type == VAL_RATIONAL) && (stack[1].type == VAL_RATIONAL)) {
        // check that result is not too big. Transform to float if
        // necessary

        if ((stack[0].r.num > 0 && stack[1].r.num > 0 &&
             stack[0].r.num > LLONG_MAX - stack[1].r.num) ||
            (stack[0].r.num < 0 && stack[1].r.num < 0 &&
             stack[0].r.num < LLONG_MIN - stack[1].r.num)) {
            cnvRationalToFloat();
            stack[0].f = stack[1].f + stack[0].f;
            return;
        }

        stack[0].r.num =
            stack[0].r.num * stack[1].r.den + stack[1].r.num * stack[0].r.den;
        stack[0].r.den = stack[0].r.den * stack[1].r.den;

        simplify(&stack[0].r);

        return;
    }

    cnvRationalToFloat();
    stack[0].f = stack[1].f + stack[0].f;

} /* doAddition */

void doSubstraction(void) {
    if ((stack[0].type == VAL_RATIONAL) && (stack[1].type == VAL_RATIONAL)) {
        long long num = stack[0].r.num;
        long long den = stack[0].r.den;

        stack[0].r.num = stack[1].r.num * den - stack[0].r.num * stack[1].r.den;
        stack[0].r.den = stack[1].r.den * den;

        simplify(&stack[0].r);
        return;
    }

    cnvRationalToFloat();
    stack[0].f = stack[1].f - stack[0].f;
} /* doSubstraction */

void doMultiplication(void) {
    if ((stack[0].type == VAL_RATIONAL) && (stack[1].type == VAL_RATIONAL)) {
        // check that result is not too big. Transform to float if
        // necessary

        if ((stack[0].r.num > 0 && stack[1].r.num > 0 &&
             stack[0].r.num > LLONG_MAX / stack[1].r.num) ||
            (stack[0].r.num < 0 && stack[1].r.num < 0 &&
             stack[0].r.num < LLONG_MIN / stack[1].r.num)) {
            cnvRationalToFloat();
            stack[0].f = stack[1].f * stack[0].f;
            return;
        }

        long long num = stack[0].r.num;
        long long den = stack[0].r.den;

        stack[0].r.num = stack[1].r.num * num;
        stack[0].r.den = stack[1].r.den * den;

        simplify(&stack[0].r);
        return;
    }

    cnvRationalToFloat();
    stack[0].f = stack[1].f * stack[0].f;
} /* doMultiplication */

void doDivision(void) {
    if ((stack[0].type == VAL_RATIONAL) && (stack[1].type == VAL_RATIONAL)) {
        long long num = stack[0].r.num;
        long long den = stack[0].r.den;

        stack[0].r.num = stack[1].r.num * den;
        stack[0].r.den = stack[1].r.den * num;

        simplify(&stack[0].r);
        return;
    }

    cnvRationalToFloat();
    stack[0].f = stack[1].f / stack[0].f;

} /* doDivision */

void initCalc(void) {
    uint16_t n = 0;

    for (n = 0; n < STACK_SIZE; n++) {
        stack[n].type = VAL_RATIONAL;
        stack[n].r.num = 0;
        stack[n].r.den = 1;
    }

    prompt[0] = 0;

    dispChanged = 1;

    degree = 1;

    base = 10;
}

void dispMenu(void) {
    uint16_t fg = RGB565(255, 255, 255);
    uint16_t bg = RGB565(0, 0, 0);

    uint16_t x = 89, x0 = 0;
    uint16_t y = 0;

    uint16_t parentId = 0;

    for (int i = 0; i < sizeof(cmds) / sizeof(Command); i++) {
        if (cmds[i].id == currentMenu) {
            parentId = cmds[i].parent;
        }

        if (cmds[i].parent == currentMenu) {
            pkk_draw_text(font16x16, x, 304, cmds[i].name, fg, bg);
            menuF[x0] = cmds[i].id;
            x += 55;
            x0++;
        }
    }

    if (currentMenu != 0) {
        pkk_draw_text(font16x16, x, 304, " < ", fg, bg);
        x += 55;
        menuF[x0] = parentId;
        x0++;
    }

    while (x0 < 4) {
        pkk_draw_text(font16x16, x, 304, "   ", fg, bg);
        x += 55;
        menuF[x0] = currentMenu;
        x0++;
    }

} /* dispMenu */

void updateHeader(void) {
    uint16_t fg = 0xFFFF;
    uint16_t bg = RGB565(75, 105, 47);

    uint16_t y = 0;
    if (degree == 1) {
        pkk_draw_text(font16x16, 0, y, "deg", fg, bg);
    } else {
        pkk_draw_text(font16x16, 0, y, "rad", fg, bg);
    }
    y += font16x16[1];
    pkk_draw_text(font16x16, 0, y, "{home}", fg, bg);
    y += font16x16[1];
}

int main() {
    pkk_init();

    initLUA();

    pkk_lcd_clear();

    pkk_displayGIF((unsigned char *)&G_background_scr, G_background_scr_length);

    initCalc();

    updateHeader();

    draw_stack();

    dispMenu();

    while (!pkk_key_pressed_withWait(KEY_SELECT)) {
        uint16_t car = pkk_key_getChar();

        if (car != 0) {
            dispChanged = 1;

            if (car == 13) {
                if (prompt[0] == 0) {
                    pushStack();
                }
                addPromptToStack();

            } else if (car == '+') {
                addPromptToStack();

                doAddition();

                removeStackAt(1);

            } else if (car == '-') {
                addPromptToStack();

                doSubstraction();

                removeStackAt(1);

            } else if (car == '*') {
                addPromptToStack();

                doMultiplication();

                removeStackAt(1);

            } else if (car == '/') {
                addPromptToStack();

                doDivision();

                removeStackAt(1);

            } else if (car == ' ') {
                showFract = !showFract;
            } else if (car == 8) {
                int pos = strlen(prompt);
                if (pos > 0) {
                    prompt[pos - 1] = 0;
                } else {
                    removeStackAt(0);
                }

            } else if (car >= '0' && car <= '9') {
                int pos = strlen(prompt);
                if (pos < 18) {
                    prompt[pos] = car;
                    prompt[pos + 1] = 0;
                }
            } else if (car == '.') {
                char *dot = strchr(prompt, '.');
                if (dot == NULL) {
                    int pos = strlen(prompt);
                    if (pos < 18) {
                        prompt[pos] = car;
                        prompt[pos + 1] = 0;
                    }
                }
            } else if ((car >= 256 + 1) && (car <= 256 + 4)) {
                u8 f = car - 257;

                switch (menuF[f]) {  // fonction
                    case 2:          // degree
                        degree = 1;
                        updateHeader();
                        break;
                    case 3:  // radian
                        degree = 0;
                        updateHeader();
                        break;
                    case 5:  // binary
                        base = 2;
                        break;
                    case 6:  // decimal
                        base = 10;
                        break;
                    case 7:  // hexadecimal
                        base = 16;
                        break;
                    default:
                        currentMenu = menuF[f];
                        break;
                }
                dispMenu();
            }
        }

        draw_stack();
        pkk_lcd_waitVSYNC();
    }

    pkk_lcd_clear();

    // TODO: save the stack to flash

    pkk_reboot();

    return 0;
} /* main */
