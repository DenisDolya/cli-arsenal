#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

#define MAX_INPUT 1024
#define MAX_VARS 100
#define HISTORY_SIZE 50
#define MAX_TOKENS 200
#define MAX_FUNC_ARGS 10
#define MATRIX_SIZE 10

// Mathematical constants
#define PI 3.14159265358979323846
#define E 2.71828182845904523536
#define PHI 1.61803398874989484820
#define GAMMA 0.57721566490153286060
#define LIGHT_SPEED 299792458.0
#define GRAVITATIONAL_CONSTANT 6.67430e-11
#define PLANCK_CONSTANT 6.62607015e-34
#define ELECTRON_CHARGE 1.602176634e-19
#define AVOGADRO 6.02214076e23
#define BOLTZMANN 1.380649e-23

// Error codes
typedef enum {
    CALC_OK,
    CALC_ERROR_SYNTAX,
    CALC_ERROR_DIV_ZERO,
    CALC_ERROR_UNDEFINED,
    CALC_ERROR_OVERFLOW,
    CALC_ERROR_MEMORY,
    CALC_ERROR_UNKNOWN_FUNCTION,
    CALC_ERROR_UNKNOWN_VARIABLE,
    CALC_ERROR_ARG_COUNT,
    CALC_ERROR_ARG_RANGE,
    CALC_ERROR_MATRIX_DIM,
    CALC_ERROR_COMPLEX_OP
} CalcError;

// Data types
typedef enum {
    DATA_REAL,
    DATA_COMPLEX,
    DATA_MATRIX
} DataType;

// Complex number
typedef struct {
    double real;
    double imag;
} ComplexNumber;

// Matrix
typedef struct {
    int rows;
    int cols;
    double data[MATRIX_SIZE][MATRIX_SIZE];
} Matrix;

// Token types
typedef enum {
    TOK_NUMBER,
    TOK_IDENTIFIER,
    TOK_OPERATOR,
    TOK_FUNCTION,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_EQUAL,
    TOK_EOF
} TokenType;

// Token structure
typedef struct {
    TokenType type;
    double value;
    char name[32];
} Token;

// Variable structure
typedef struct {
    char name[32];
    DataType type;
    union {
        double real;
        ComplexNumber complex_num;
        Matrix matrix;
    } value;
    int constant;
} Variable;

// Function pointer type
typedef double (*MathFunc)(double[], int);

// Function definition structure
typedef struct {
    char name[32];
    MathFunc func;
    int min_args;
    int max_args;
} FunctionDef;

// Calculation history
typedef struct {
    char expression[MAX_INPUT];
    double result;
} HistoryEntry;

// Calculator state
typedef struct {
    Variable variables[MAX_VARS];
    int var_count;
    HistoryEntry history[HISTORY_SIZE];
    int history_index;
    int history_count;
    int angle_mode; // 0 = radians, 1 = degrees
    int precision;  // Number of decimal places to display
} Calculator;

// Function declarations
void init_calculator(Calculator *calc);
double evaluate_expression(Calculator *calc, const char *expr, CalcError *error);
void print_error(CalcError error);
void show_help(void);
void show_functions(void);
void show_constants(void);
void show_variables(Calculator *calc);
void show_history(Calculator *calc);
int handle_command(Calculator *calc, const char *input);
void add_history(Calculator *calc, const char *expr, double result);
int is_constant(const char *name);
double get_constant_value(const char *name);
double factorial(double n);
double calculate_function(Calculator *calc, const char *func_name, double args[], int arg_count, CalcError *error);
int set_variable(Calculator *calc, const char *name, double value, int constant);

// Complex number operations
ComplexNumber complex_add(ComplexNumber a, ComplexNumber b);
ComplexNumber complex_sub(ComplexNumber a, ComplexNumber b);
ComplexNumber complex_mul(ComplexNumber a, ComplexNumber b);
ComplexNumber complex_div(ComplexNumber a, ComplexNumber b);
ComplexNumber complex_pow(ComplexNumber a, ComplexNumber b);
double complex_abs(ComplexNumber a);
ComplexNumber complex_sqrt(ComplexNumber a);
ComplexNumber complex_exp(ComplexNumber a);
ComplexNumber complex_log(ComplexNumber a);
ComplexNumber complex_sin(ComplexNumber a);
ComplexNumber complex_cos(ComplexNumber a);
ComplexNumber complex_tan(ComplexNumber a);

// Matrix operations
Matrix matrix_add(Matrix a, Matrix b);
Matrix matrix_sub(Matrix a, Matrix b);
Matrix matrix_mul(Matrix a, Matrix b);
Matrix matrix_scalar_mul(Matrix a, double scalar);
double matrix_det(Matrix a);
Matrix matrix_inv(Matrix a);
Matrix matrix_transpose(Matrix a);

// Tokenizer functions
Token* tokenize(const char *expr, int *token_count, CalcError *error);
double parse_expression(Calculator *calc, Token *tokens, int *pos, CalcError *error);
double parse_term(Calculator *calc, Token *tokens, int *pos, CalcError *error);
double parse_factor(Calculator *calc, Token *tokens, int *pos, CalcError *error);

// Mathematical functions
double func_sin(double args[], int count);
double func_cos(double args[], int count);
double func_tan(double args[], int count);
double func_asin(double args[], int count);
double func_acos(double args[], int count);
double func_atan(double args[], int count);
double func_atan2(double args[], int count);
double func_sinh(double args[], int count);
double func_cosh(double args[], int count);
double func_tanh(double args[], int count);
double func_asinh(double args[], int count);
double func_acosh(double args[], int count);
double func_atanh(double args[], int count);
double func_log(double args[], int count);
double func_log10(double args[], int count);
double func_log2(double args[], int count);
double func_exp(double args[], int count);
double func_sqrt(double args[], int count);
double func_cbrt(double args[], int count);
double func_pow(double args[], int count);
double func_abs(double args[], int count);
double func_floor(double args[], int count);
double func_ceil(double args[], int count);
double func_round(double args[], int count);
double func_min(double args[], int count);
double func_max(double args[], int count);
double func_sum(double args[], int count);
double func_mean(double args[], int count);
double func_median(double args[], int count);
double func_factorial(double args[], int count);
double func_gcd(double args[], int count);
double func_lcm(double args[], int count);
double func_deg2rad(double args[], int count);
double func_rad2deg(double args[], int count);
double func_perm(double args[], int count);
double func_comb(double args[], int count);
double func_rand(double args[], int count);
double func_det(double args[], int count);
double func_trace(double args[], int count);

// Function table
FunctionDef function_table[] = {
    {"sin", func_sin, 1, 1},
    {"cos", func_cos, 1, 1},
    {"tan", func_tan, 1, 1},
    {"asin", func_asin, 1, 1},
    {"acos", func_acos, 1, 1},
    {"atan", func_atan, 1, 1},
    {"atan2", func_atan2, 2, 2},
    {"sinh", func_sinh, 1, 1},
    {"cosh", func_cosh, 1, 1},
    {"tanh", func_tanh, 1, 1},
    {"asinh", func_asinh, 1, 1},
    {"acosh", func_acosh, 1, 1},
    {"atanh", func_atanh, 1, 1},
    {"log", func_log, 1, 1},
    {"log10", func_log10, 1, 1},
    {"log2", func_log2, 1, 1},
    {"exp", func_exp, 1, 1},
    {"sqrt", func_sqrt, 1, 1},
    {"cbrt", func_cbrt, 1, 1},
    {"pow", func_pow, 2, 2},
    {"abs", func_abs, 1, 1},
    {"floor", func_floor, 1, 1},
    {"ceil", func_ceil, 1, 1},
    {"round", func_round, 1, 1},
    {"min", func_min, 1, 0},
    {"max", func_max, 1, 0},
    {"sum", func_sum, 1, 0},
    {"mean", func_mean, 1, 0},
    {"median", func_median, 1, 0},
    {"factorial", func_factorial, 1, 1},
    {"gcd", func_gcd, 2, 0},
    {"lcm", func_lcm, 2, 0},
    {"deg2rad", func_deg2rad, 1, 1},
    {"rad2deg", func_rad2deg, 1, 1},
    {"perm", func_perm, 2, 2},
    {"comb", func_comb, 2, 2},
    {"rand", func_rand, 0, 2},
    {"det", func_det, 1, 1},
    {"trace", func_trace, 1, 1},
    {"", NULL, 0, 0}  // Sentinel
};

// Initialize calculator with default variables and constants
void init_calculator(Calculator *calc) {
    calc->var_count = 0;
    calc->history_index = 0;
    calc->history_count = 0;
    calc->angle_mode = 0; // Default to radians
    calc->precision = 10; // Default precision
    
    // Add constants
    set_variable(calc, "pi", PI, 1);
    set_variable(calc, "e", E, 1);
    set_variable(calc, "phi", PHI, 1);
    set_variable(calc, "gamma", GAMMA, 1);
    set_variable(calc, "c", LIGHT_SPEED, 1);
    set_variable(calc, "G", GRAVITATIONAL_CONSTANT, 1);
    set_variable(calc, "h", PLANCK_CONSTANT, 1);
    set_variable(calc, "q", ELECTRON_CHARGE, 1);
    set_variable(calc, "Na", AVOGADRO, 1);
    set_variable(calc, "k", BOLTZMANN, 1);
    set_variable(calc, "inf", INFINITY, 1);
    set_variable(calc, "i", 0, 1); // Complex unit (special handling)
}

// Find a variable by name
Variable* find_variable(Calculator *calc, const char *name) {
    for (int i = 0; i < calc->var_count; i++) {
        if (strcmp(calc->variables[i].name, name) == 0) {
            return &calc->variables[i];
        }
    }
    return NULL;
}

// Add or update a variable
int set_variable(Calculator *calc, const char *name, double value, int constant) {
    Variable *var = find_variable(calc, name);
    
    if (var != NULL) {
        if (var->constant) {
            return 0; // Cannot modify constant
        }
        var->value.real = value;
        return 1;
    }
    
    if (calc->var_count >= MAX_VARS) {
        return 0; // Too many variables
    }
    
    strncpy(calc->variables[calc->var_count].name, name, 31);
    calc->variables[calc->var_count].name[31] = '\0';
    calc->variables[calc->var_count].value.real = value;
    calc->variables[calc->var_count].type = DATA_REAL;
    calc->variables[calc->var_count].constant = constant;
    calc->var_count++;
    return 1;
}

// Add entry to history
void add_history(Calculator *calc, const char *expr, double result) {
    strncpy(calc->history[calc->history_index].expression, expr, MAX_INPUT - 1);
    calc->history[calc->history_index].expression[MAX_INPUT - 1] = '\0';
    calc->history[calc->history_index].result = result;
    
    calc->history_index = (calc->history_index + 1) % HISTORY_SIZE;
    if (calc->history_count < HISTORY_SIZE) {
        calc->history_count++;
    }
}

// Show calculation history
void show_history(Calculator *calc) {
    printf("\nCalculation History:\n");
    printf("-------------------\n");
    
    if (calc->history_count == 0) {
        printf("No history available.\n");
        return;
    }
    
    int start_index = (calc->history_index - calc->history_count + HISTORY_SIZE) % HISTORY_SIZE;
    for (int i = 0; i < calc->history_count; i++) {
        int index = (start_index + i) % HISTORY_SIZE;
        printf("%d: %s = %.*g\n", i + 1, 
               calc->history[index].expression, 
               calc->precision,
               calc->history[index].result);
    }
}

// Show all variables
void show_variables(Calculator *calc) {
    printf("\nVariables:\n");
    printf("----------\n");
    
    if (calc->var_count == 0) {
        printf("No variables defined.\n");
        return;
    }
    
    for (int i = 0; i < calc->var_count; i++) {
        printf("%s = %.*g", calc->variables[i].name, calc->precision, calc->variables[i].value.real);
        if (calc->variables[i].constant) {
            printf(" (constant)");
        }
        printf("\n");
    }
}

// Show available functions
void show_functions() {
    printf("\nMathematical Functions:\n");
    printf("-----------------------\n");
    printf("Trigonometric:    sin, cos, tan, asin, acos, atan, atan2\n");
    printf("Hyperbolic:       sinh, cosh, tanh, asinh, acosh, atanh\n");
    printf("Exponential:      exp, log, log10, log2, pow, sqrt, cbrt\n");
    printf("Rounding:         abs, floor, ceil, round\n");
    printf("Statistical:      min, max, sum, mean, median\n");
    printf("Combinatorics:    factorial, perm, comb, gcd, lcm\n");
    printf("Unit Conversion:  deg2rad, rad2deg\n");
    printf("Matrix Operations: det, trace\n");
    printf("Random:           rand\n");
}

// Show mathematical constants
void show_constants() {
    printf("\nMathematical Constants:\n");
    printf("-----------------------\n");
    printf("pi  = %.15g (π, circle constant)\n", PI);
    printf("e   = %.15g (Euler's number)\n", E);
    printf("phi = %.15g (Golden ratio)\n", PHI);
    printf("gamma = %.15g (Euler-Mascheroni constant)\n", GAMMA);
    printf("c   = %.6g (Speed of light in m/s)\n", LIGHT_SPEED);
    printf("G   = %.6g (Gravitational constant)\n", GRAVITATIONAL_CONSTANT);
    printf("h   = %.6g (Planck constant)\n", PLANCK_CONSTANT);
    printf("q   = %.6g (Electron charge)\n", ELECTRON_CHARGE);
    printf("Na  = %.6g (Avogadro's number)\n", AVOGADRO);
    printf("k   = %.6g (Boltzmann constant)\n", BOLTZMANN);
    printf("inf = infinity\n");
    printf("i   = imaginary unit\n");
}

// Show help information
void show_help() {
    printf("\nCalculator Help:\n");
    printf("================\n");
    printf("Basic operations: +, -, *, /, ^ (power), %% (modulo), ! (factorial)\n");
    printf("Assignment:       x = 5 (create variable), const x = 10 (create constant)\n");
    printf("Grouping:         Use parentheses () for complex expressions\n");
    printf("Functions:        func(arg1, arg2, ...) - see 'functions' for list\n");
    printf("Constants:        Predefined mathematical constants - see 'constants'\n");
    printf("Angle mode:       Use 'deg' for degrees mode, 'rad' for radians mode\n");
    printf("Precision:        Use 'precision n' to set decimal places (0-15)\n");
    printf("Complex numbers:  Use 'i' for imaginary unit (e.g., 3+4i)\n");
    printf("\nCommands:\n");
    printf("---------\n");
    printf("help       - Show this help message\n");
    printf("functions  - List all available functions\n");
    printf("constants  - List mathematical constants\n");
    printf("variables  - Show all defined variables\n");
    printf("history    - Show calculation history\n");
    printf("deg        - Set angle mode to degrees\n");
    printf("rad        - Set angle mode to radians\n");
    printf("precision n- Set display precision to n decimal places\n");
    printf("clear      - Clear the screen\n");
    printf("exit, quit - Exit the calculator\n");
}

// Print error message
void print_error(CalcError error) {
    switch (error) {
        case CALC_ERROR_SYNTAX:
            printf("Error: Syntax error in expression\n");
            break;
        case CALC_ERROR_DIV_ZERO:
            printf("Error: Division by zero\n");
            break;
        case CALC_ERROR_UNDEFINED:
            printf("Error: Undefined result (e.g., sqrt of negative number)\n");
            break;
        case CALC_ERROR_OVERFLOW:
            printf("Error: Numerical overflow\n");
            break;
        case CALC_ERROR_MEMORY:
            printf("Error: Memory allocation failed\n");
            break;
        case CALC_ERROR_UNKNOWN_FUNCTION:
            printf("Error: Unknown function\n");
            break;
        case CALC_ERROR_UNKNOWN_VARIABLE:
            printf("Error: Unknown variable\n");
            break;
        case CALC_ERROR_ARG_COUNT:
            printf("Error: Incorrect number of arguments for function\n");
            break;
        case CALC_ERROR_ARG_RANGE:
            printf("Error: Argument out of valid range\n");
            break;
        case CALC_ERROR_MATRIX_DIM:
            printf("Error: Matrix dimension mismatch\n");
            break;
        case CALC_ERROR_COMPLEX_OP:
            printf("Error: Complex number operation not supported\n");
            break;
        default:
            printf("Error: Unknown error\n");
            break;
    }
}

// Check if a string is a known constant
int is_constant(const char *name) {
    const char *constants[] = {"pi", "e", "phi", "gamma", "c", "G", "h", "q", "Na", "k", "inf", "i", NULL};
    
    for (int i = 0; constants[i] != NULL; i++) {
        if (strcmp(name, constants[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Get value of a constant
double get_constant_value(const char *name) {
    if (strcmp(name, "pi") == 0) return PI;
    if (strcmp(name, "e") == 0) return E;
    if (strcmp(name, "phi") == 0) return PHI;
    if (strcmp(name, "gamma") == 0) return GAMMA;
    if (strcmp(name, "c") == 0) return LIGHT_SPEED;
    if (strcmp(name, "G") == 0) return GRAVITATIONAL_CONSTANT;
    if (strcmp(name, "h") == 0) return PLANCK_CONSTANT;
    if (strcmp(name, "q") == 0) return ELECTRON_CHARGE;
    if (strcmp(name, "Na") == 0) return AVOGADRO;
    if (strcmp(name, "k") == 0) return BOLTZMANN;
    if (strcmp(name, "inf") == 0) return INFINITY;
    if (strcmp(name, "i") == 0) return 0; // Special case for complex unit
    return 0.0;
}

// Find function definition by name
FunctionDef* find_function(const char *name) {
    for (int i = 0; function_table[i].func != NULL; i++) {
        if (strcmp(function_table[i].name, name) == 0) {
            return &function_table[i];
        }
    }
    return NULL;
}

// Calculate factorial
double factorial(double n) {
    if (n < 0 || n != floor(n)) {
        return NAN;
    }
    
    double result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
        if (isinf(result)) {
            return INFINITY;
        }
    }
    return result;
}

// Calculate GCD of two integers
long long gcd(long long a, long long b) {
    a = llabs(a);
    b = llabs(b);
    
    while (b != 0) {
        long long temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

// Calculate LCM of two integers
long long lcm(long long a, long long b) {
    if (a == 0 || b == 0) return 0;
    return llabs(a * b) / gcd(a, b);
}

// Matrix determinant
double matrix_det(Matrix a) {
    if (a.rows != a.cols) return NAN;
    
    if (a.rows == 1) return a.data[0][0];
    if (a.rows == 2) return a.data[0][0] * a.data[1][1] - a.data[0][1] * a.data[1][0];
    
    double det = 0;
    for (int col = 0; col < a.cols; col++) {
        Matrix submatrix;
        submatrix.rows = a.rows - 1;
        submatrix.cols = a.cols - 1;
        
        for (int i = 1; i < a.rows; i++) {
            int subcol = 0;
            for (int j = 0; j < a.cols; j++) {
                if (j == col) continue;
                submatrix.data[i-1][subcol++] = a.data[i][j];
            }
        }
        
        double sign = (col % 2 == 0) ? 1 : -1;
        det += sign * a.data[0][col] * matrix_det(submatrix);
    }
    
    return det;
}

// Matrix trace
double matrix_trace(Matrix a) {
    if (a.rows != a.cols) return NAN;
    
    double trace = 0;
    for (int i = 0; i < a.rows; i++) {
        trace += a.data[i][i];
    }
    return trace;
}

// Mathematical function implementations
double func_sin(double args[], int count) {
    return sin(args[0]);
}

double func_cos(double args[], int count) {
    return cos(args[0]);
}

double func_tan(double args[], int count) {
    return tan(args[0]);
}

double func_asin(double args[], int count) {
    if (args[0] < -1 || args[0] > 1) return NAN;
    return asin(args[0]);
}

double func_acos(double args[], int count) {
    if (args[0] < -1 || args[0] > 1) return NAN;
    return acos(args[0]);
}

double func_atan(double args[], int count) {
    return atan(args[0]);
}

double func_atan2(double args[], int count) {
    return atan2(args[0], args[1]);
}

double func_sinh(double args[], int count) {
    return sinh(args[0]);
}

double func_cosh(double args[], int count) {
    return cosh(args[0]);
}

double func_tanh(double args[], int count) {
    return tanh(args[0]);
}

double func_asinh(double args[], int count) {
    return asinh(args[0]);
}

double func_acosh(double args[], int count) {
    if (args[0] < 1) return NAN;
    return acosh(args[0]);
}

double func_atanh(double args[], int count) {
    if (args[0] <= -1 || args[0] >= 1) return NAN;
    return atanh(args[0]);
}

double func_log(double args[], int count) {
    if (args[0] <= 0) return NAN;
    return log(args[0]);
}

double func_log10(double args[], int count) {
    if (args[0] <= 0) return NAN;
    return log10(args[0]);
}

double func_log2(double args[], int count) {
    if (args[0] <= 0) return NAN;
    return log2(args[0]);
}

double func_exp(double args[], int count) {
    return exp(args[0]);
}

double func_sqrt(double args[], int count) {
    if (args[0] < 0) return NAN;
    return sqrt(args[0]);
}

double func_cbrt(double args[], int count) {
    return cbrt(args[0]);
}

double func_pow(double args[], int count) {
    return pow(args[0], args[1]);
}

double func_abs(double args[], int count) {
    return fabs(args[0]);
}

double func_floor(double args[], int count) {
    return floor(args[0]);
}

double func_ceil(double args[], int count) {
    return ceil(args[0]);
}

double func_round(double args[], int count) {
    return round(args[0]);
}

double func_min(double args[], int count) {
    double min_val = args[0];
    for (int i = 1; i < count; i++) {
        if (args[i] < min_val) min_val = args[i];
    }
    return min_val;
}

double func_max(double args[], int count) {
    double max_val = args[0];
    for (int i = 1; i < count; i++) {
        if (args[i] > max_val) max_val = args[i];
    }
    return max_val;
}

double func_sum(double args[], int count) {
    double sum = 0;
    for (int i = 0; i < count; i++) {
        sum += args[i];
    }
    return sum;
}

double func_mean(double args[], int count) {
    if (count == 0) return 0;
    return func_sum(args, count) / count;
}

double func_median(double args[], int count) {
    if (count == 0) return 0;
    
    // Sort the arguments
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (args[i] > args[j]) {
                double temp = args[i];
                args[i] = args[j];
                args[j] = temp;
            }
        }
    }
    
    if (count % 2 == 1) {
        return args[count / 2];
    } else {
        return (args[count / 2 - 1] + args[count / 2]) / 2;
    }
}

double func_factorial(double args[], int count) {
    return factorial(args[0]);
}

double func_gcd(double args[], int count) {
    long long a = (long long)args[0];
    long long b = (long long)args[1];
    return (double)gcd(a, b);
}

double func_lcm(double args[], int count) {
    long long a = (long long)args[0];
    long long b = (long long)args[1];
    return (double)lcm(a, b);
}

double func_deg2rad(double args[], int count) {
    return args[0] * PI / 180.0;
}

double func_rad2deg(double args[], int count) {
    return args[0] * 180.0 / PI;
}

double func_perm(double args[], int count) {
    long long n = (long long)args[0];
    long long k = (long long)args[1];
    
    if (n < 0 || k < 0 || k > n) return NAN;
    
    long long result = 1;
    for (long long i = 0; i < k; i++) {
        result *= (n - i);
        if (result < 0) return INFINITY; // Overflow
    }
    return (double)result;
}

double func_comb(double args[], int count) {
    long long n = (long long)args[0];
    long long k = (long long)args[1];
    
    if (n < 0 || k < 0 || k > n) return NAN;
    
    // Use smaller of k and n-k
    if (k > n - k) k = n - k;
    
    long long result = 1;
    for (long long i = 1; i <= k; i++) {
        result = result * (n - k + i) / i;
        if (result < 0) return INFINITY; // Overflow
    }
    return (double)result;
}

double func_rand(double args[], int count) {
    if (count == 0) {
        return (double)rand() / RAND_MAX;
    } else if (count == 1) {
        return (double)rand() / RAND_MAX * args[0];
    } else {
        return args[0] + (double)rand() / RAND_MAX * (args[1] - args[0]);
    }
}

double func_det(double args[], int count) {
    // For simplicity, assume a 2x2 matrix with 4 elements
    if (count != 4) return NAN;
    
    Matrix m;
    m.rows = 2;
    m.cols = 2;
    m.data[0][0] = args[0];
    m.data[0][1] = args[1];
    m.data[1][0] = args[2];
    m.data[1][1] = args[3];
    
    return matrix_det(m);
}

double func_trace(double args[], int count) {
    // For simplicity, assume a 2x2 matrix with 4 elements
    if (count != 4) return NAN;
    
    Matrix m;
    m.rows = 2;
    m.cols = 2;
    m.data[0][0] = args[0];
    m.data[0][1] = args[1];
    m.data[1][0] = args[2];
    m.data[1][1] = args[3];
    
    return matrix_trace(m);
}

// Calculate a function with error checking
double calculate_function(Calculator *calc, const char *func_name, double args[], int arg_count, CalcError *error) {
    FunctionDef *func_def = find_function(func_name);
    if (func_def == NULL) {
        *error = CALC_ERROR_UNKNOWN_FUNCTION;
        return 0;
    }
    
    if (arg_count < func_def->min_args || (func_def->max_args > 0 && arg_count > func_def->max_args)) {
        *error = CALC_ERROR_ARG_COUNT;
        return 0;
    }
    
    // Handle angle conversion for trigonometric functions
    if (calc->angle_mode == 1) { // Degrees mode
        if (strcmp(func_name, "sin") == 0 || strcmp(func_name, "cos") == 0 || 
            strcmp(func_name, "tan") == 0 || strcmp(func_name, "asin") == 0 ||
            strcmp(func_name, "acos") == 0 || strcmp(func_name, "atan") == 0) {
            if (strcmp(func_name, "sin") == 0 || strcmp(func_name, "cos") == 0 || 
                strcmp(func_name, "tan") == 0) {
                args[0] = args[0] * PI / 180.0;
            } else {
                double result = func_def->func(args, arg_count);
                if (!isnan(result)) {
                    result = result * 180.0 / PI;
                }
                return result;
            }
        }
    }
    
    double result = func_def->func(args, arg_count);
    if (isnan(result)) {
        *error = CALC_ERROR_UNDEFINED;
    } else if (isinf(result)) {
        *error = CALC_ERROR_OVERFLOW;
    }
    
    return result;
}

// Tokenize the input expression
Token* tokenize(const char *expr, int *token_count, CalcError *error) {
    Token *tokens = malloc(MAX_TOKENS * sizeof(Token));
    if (tokens == NULL) {
        *error = CALC_ERROR_MEMORY;
        return NULL;
    }
    
    *token_count = 0;
    const char *p = expr;
    
    while (*p && *token_count < MAX_TOKENS) {
        // Skip whitespace
        if (isspace(*p)) {
            p++;
            continue;
        }
        
        // Number
        if (isdigit(*p) || (*p == '.' && isdigit(*(p+1))) || 
            (*p == '-' && (isdigit(*(p+1)) || *(p+1) == '.'))) {
            char *end;
            double value = strtod(p, &end);
            
            tokens[*token_count].type = TOK_NUMBER;
            tokens[*token_count].value = value;
            *token_count += 1;
            
            p = end;
            continue;
        }
        
        // Identifier or function
        if (isalpha(*p) || *p == '_') {
            int i = 0;
            while ((isalnum(*p) || *p == '_') && i < 31) {
                tokens[*token_count].name[i++] = *p++;
            }
            tokens[*token_count].name[i] = '\0';
            
            // Check if it's followed by '(' - then it's a function
            const char *next = p;
            while (isspace(*next)) next++;
            
            if (*next == '(') {
                tokens[*token_count].type = TOK_FUNCTION;
            } else {
                tokens[*token_count].type = TOK_IDENTIFIER;
            }
            
            *token_count += 1;
            continue;
        }
        
        // Operators
        if (strchr("+-*/^!%", *p) != NULL) {
            tokens[*token_count].type = TOK_OPERATOR;
            tokens[*token_count].name[0] = *p;
            tokens[*token_count].name[1] = '\0';
            *token_count += 1;
            p++;
            continue;
        }
        
        // Parentheses and comma
        if (*p == '(') {
            tokens[*token_count].type = TOK_LPAREN;
            *token_count += 1;
            p++;
            continue;
        }
        
        if (*p == ')') {
            tokens[*token_count].type = TOK_RPAREN;
            *token_count += 1;
            p++;
            continue;
        }
        
        if (*p == ',') {
            tokens[*token_count].type = TOK_COMMA;
            *token_count += 1;
            p++;
            continue;
        }
        
        if (*p == '=') {
            tokens[*token_count].type = TOK_EQUAL;
            *token_count += 1;
            p++;
            continue;
        }
        
        // Unknown character
        *error = CALC_ERROR_SYNTAX;
        free(tokens);
        return NULL;
    }
    
    // Add EOF token
    tokens[*token_count].type = TOK_EOF;
    *token_count += 1;
    
    return tokens;
}

// Parse factor (numbers, identifiers, functions, parentheses)
double parse_factor(Calculator *calc, Token *tokens, int *pos, CalcError *error) {
    Token token = tokens[*pos];
    
    if (token.type == TOK_NUMBER) {
        (*pos)++;
        return token.value;
    }
    
    if (token.type == TOK_IDENTIFIER) {
        (*pos)++;
        
        // Check if it's a constant
        if (is_constant(token.name)) {
            return get_constant_value(token.name);
        }
        
        // Check if it's a variable
        Variable *var = find_variable(calc, token.name);
        if (var != NULL) {
            return var->value.real;
        }
        
        *error = CALC_ERROR_UNKNOWN_VARIABLE;
        return 0;
    }
    
    if (token.type == TOK_FUNCTION) {
        (*pos)++;
        
        // Check for opening parenthesis
        if (tokens[*pos].type != TOK_LPAREN) {
            *error = CALC_ERROR_SYNTAX;
            return 0;
        }
        (*pos)++;
        
        // Parse function arguments
        double args[MAX_FUNC_ARGS];
        int arg_count = 0;
        
        while (tokens[*pos].type != TOK_RPAREN && arg_count < MAX_FUNC_ARGS) {
            if (arg_count > 0) {
                if (tokens[*pos].type != TOK_COMMA) {
                    *error = CALC_ERROR_SYNTAX;
                    return 0;
                }
                (*pos)++;
            }
            
            args[arg_count] = parse_expression(calc, tokens, pos, error);
            if (*error != CALC_OK) return 0;
            arg_count++;
        }
        
        // Check for closing parenthesis
        if (tokens[*pos].type != TOK_RPAREN) {
            *error = CALC_ERROR_SYNTAX;
            return 0;
        }
        (*pos)++;
        
        return calculate_function(calc, token.name, args, arg_count, error);
    }
    
    if (token.type == TOK_LPAREN) {
        (*pos)++;
        double result = parse_expression(calc, tokens, pos, error);
        
        if (tokens[*pos].type != TOK_RPAREN) {
            *error = CALC_ERROR_SYNTAX;
            return 0;
        }
        (*pos)++;
        
        return result;
    }
    
    if (token.type == TOK_OPERATOR && token.name[0] == '-') {
        (*pos)++;
        return -parse_factor(calc, tokens, pos, error);
    }
    
    if (token.type == TOK_OPERATOR && token.name[0] == '+') {
        (*pos)++;
        return parse_factor(calc, tokens, pos, error);
    }
    
    *error = CALC_ERROR_SYNTAX;
    return 0;
}

// Parse term (multiplication, division, modulo)
double parse_term(Calculator *calc, Token *tokens, int *pos, CalcError *error) {
    double result = parse_factor(calc, tokens, pos, error);
    if (*error != CALC_OK) return 0;
    
    while (1) {
        Token token = tokens[*pos];
        
        if (token.type == TOK_OPERATOR) {
            if (token.name[0] == '*') {
                (*pos)++;
                result *= parse_factor(calc, tokens, pos, error);
                if (*error != CALC_OK) return 0;
            } else if (token.name[0] == '/') {
                (*pos)++;
                double divisor = parse_factor(calc, tokens, pos, error);
                if (*error != CALC_OK) return 0;
                
                if (divisor == 0) {
                    *error = CALC_ERROR_DIV_ZERO;
                    return 0;
                }
                
                result /= divisor;
            } else if (token.name[0] == '%') {
                (*pos)++;
                double divisor = parse_factor(calc, tokens, pos, error);
                if (*error != CALC_OK) return 0;
                
                if (divisor == 0) {
                    *error = CALC_ERROR_DIV_ZERO;
                    return 0;
                }
                
                result = fmod(result, divisor);
            } else {
                break;
            }
        } else {
            break;
        }
    }
    
    return result;
}

// Parse expression (addition, subtraction)
double parse_expression(Calculator *calc, Token *tokens, int *pos, CalcError *error) {
    double result = parse_term(calc, tokens, pos, error);
    if (*error != CALC_OK) return 0;
    
    while (1) {
        Token token = tokens[*pos];
        
        if (token.type == TOK_OPERATOR) {
            if (token.name[0] == '+') {
                (*pos)++;
                result += parse_term(calc, tokens, pos, error);
                if (*error != CALC_OK) return 0;
            } else if (token.name[0] == '-') {
                (*pos)++;
                result -= parse_term(calc, tokens, pos, error);
                if (*error != CALC_OK) return 0;
            } else {
                break;
            }
        } else {
            break;
        }
    }
    
    return result;
}

// Parse assignment or expression
double parse_assignment(Calculator *calc, Token *tokens, int *pos, CalcError *error) {
    // Check if this is an assignment
    if (tokens[*pos].type == TOK_IDENTIFIER && tokens[*pos + 1].type == TOK_EQUAL) {
        char var_name[32];
        strcpy(var_name, tokens[*pos].name);
        *pos += 2; // Skip identifier and equals
        
        double value = parse_expression(calc, tokens, pos, error);
        if (*error != CALC_OK) return 0;
        
        // Check if it's a constant assignment
        int is_constant = 0;
        if (strcmp(var_name, "const") == 0) {
            is_constant = 1;
            if (tokens[*pos].type != TOK_IDENTIFIER) {
                *error = CALC_ERROR_SYNTAX;
                return 0;
            }
            strcpy(var_name, tokens[*pos].name);
            *pos += 1;
            
            if (tokens[*pos].type != TOK_EQUAL) {
                *error = CALC_ERROR_SYNTAX;
                return 0;
            }
            *pos += 1;
            
            value = parse_expression(calc, tokens, pos, error);
            if (*error != CALC_OK) return 0;
        }
        
        if (!set_variable(calc, var_name, value, is_constant)) {
            *error = CALC_ERROR_MEMORY;
            return 0;
        }
        
        return value;
    }
    
    // Regular expression
    return parse_expression(calc, tokens, pos, error);
}

// Evaluate expression with proper parsing
double evaluate_expression(Calculator *calc, const char *expr, CalcError *error) {
    int token_count = 0;
    Token *tokens = tokenize(expr, &token_count, error);
    if (*error != CALC_OK) {
        return 0;
    }
    
    int pos = 0;
    double result = parse_assignment(calc, tokens, &pos, error);
    
    // Check if we parsed the entire expression
    if (pos < token_count - 1 && tokens[pos].type != TOK_EOF) {
        *error = CALC_ERROR_SYNTAX;
    }
    
    free(tokens);
    return result;
}

// Handle special commands
int handle_command(Calculator *calc, const char *input) {
    if (strcmp(input, "help") == 0) {
        show_help();
        return 1;
    } else if (strcmp(input, "functions") == 0) {
        show_functions();
        return 1;
    } else if (strcmp(input, "constants") == 0) {
        show_constants();
        return 1;
    } else if (strcmp(input, "variables") == 0) {
        show_variables(calc);
        return 1;
    } else if (strcmp(input, "history") == 0) {
        show_history(calc);
        return 1;
    } else if (strcmp(input, "deg") == 0) {
        calc->angle_mode = 1;
        printf("Angle mode set to degrees\n");
        return 1;
    } else if (strcmp(input, "rad") == 0) {
        calc->angle_mode = 0;
        printf("Angle mode set to radians\n");
        return 1;
    } else if (strncmp(input, "precision", 9) == 0) {
        int prec;
        if (sscanf(input + 9, "%d", &prec) == 1 && prec >= 0 && prec <= 15) {
            calc->precision = prec;
            printf("Precision set to %d decimal places\n", prec);
        } else {
            printf("Invalid precision. Use 'precision n' where n is 0-15\n");
        }
        return 1;
    } else if (strcmp(input, "clear") == 0) {
        #ifdef _WIN32
            system("cls");
        #else
            system("clear");
        #endif
        return 1;
    } else if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
        return -1;
    }
    return 0;
}

int main() {
    Calculator calc;
    char input[MAX_INPUT];
    
    init_calculator(&calc);
    
    printf("=============================================\n");
    printf("    Advanced Scientific Calculator\n");
    printf("=============================================\n");
    printf("Type 'help' for available commands and functions\n");
    printf("Type 'exit' or 'quit' to exit the calculator\n\n");
    
    // Seed random number generator
    srand(time(NULL));
    
    while (1) {
        printf(">> ");
        
        if (!fgets(input, MAX_INPUT, stdin)) {
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        // Skip empty input
        if (strlen(input) == 0) {
            continue;
        }
        
        // Handle commands
        int cmd_result = handle_command(&calc, input);
        if (cmd_result == -1) {
            break;
        } else if (cmd_result == 1) {
            continue;
        }
        
        // Evaluate expression
        CalcError error = CALC_OK;
        double result = evaluate_expression(&calc, input, &error);
        
        if (error == CALC_OK) {
            printf("= %.*g\n", calc.precision, result);
            add_history(&calc, input, result);
        } else {
            print_error(error);
        }
    }
    
    printf("Goodbye!\n");
    return 0;
}