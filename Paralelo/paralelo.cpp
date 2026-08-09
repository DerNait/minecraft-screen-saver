// COPIA DE SECUENCIAL MIENTRAS!!!

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <omp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <random>

// Constantes de configuracion
static const int   WINDOW_WIDTH  = 1280;
static const int   WINDOW_HEIGHT = 720;
static const float GRAVITY       = 9.8f;
static const float BLOCK_SPACING = 1.0f;

// Elemento a renderizar: un bloque del terreno.
struct Block {
    glm::vec3 targetPos;
    glm::vec3 pos;
    glm::vec3 color;
    float velocityY = 0.0f;
    bool  placed    = false;
};

std::vector<Block> terrain; // almacenamiento de los N elementos a renderizar

// Inicializa los N bloques en una grilla simple
// Cada bloque arranca cayendo del cielo por encima de su posicion final.
void initTerrain(int n) {
    terrain.clear();
    terrain.reserve(n);

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> colorJitter(0.0f, 1.0f);
    std::uniform_real_distribution<float> spawnHeight(15.0f, 30.0f);

    int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));

    for (int i = 0; i < n; ++i) {
        int gx = i % side;
        int gz = i / side;

        Block b;
        b.targetPos = glm::vec3(
            (gx - side / 2.0f) * BLOCK_SPACING,
            0.0f,
            (gz - side / 2.0f) * BLOCK_SPACING
        );
        b.pos = glm::vec3(b.targetPos.x, spawnHeight(rng), b.targetPos.z);
        b.color = glm::vec3(0.2f + 0.2f * colorJitter(rng),
                             0.55f + 0.25f * colorJitter(rng),
                             0.2f + 0.2f * colorJitter(rng));
        b.velocityY = 0.0f;
        b.placed = false;

        terrain.push_back(b);
    }
}

// Funcion de actualizacion: determina la siguiente posicion de un bloque
// usando una simple integracion de gravedad hasta que llega a targetPos.
void updateBlock(Block& b, float dt) {
    if (b.placed) return;

    b.velocityY += GRAVITY * dt;
    b.pos.y -= b.velocityY * dt;

    if (b.pos.y <= b.targetPos.y) {
        b.pos.y = b.targetPos.y;
        b.velocityY = 0.0f;
        b.placed = true;
    }
}

void updateTerrain(float dt) {
    // TODO: dividir terrain entre hilos
    for (auto& b : terrain) {
        updateBlock(b, dt);
    }
}

// Geometria del cubo unitario compartido por todas las instancias.
static const float CUBE_VERTICES[] = {
    // posiciones
    -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,
     0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,

    -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
     0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f,

    -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
    -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,

     0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f,
     0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,

    -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,
     0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f,

    -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
     0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f,
};

static const char* VERTEX_SHADER_SRC = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in mat4 aInstanceModel; // ocupa locations 1,2,3,4
layout(location = 5) in vec3 aInstanceColor;

uniform mat4 uViewProj;

out vec3 vColor;

void main() {
    gl_Position = uViewProj * aInstanceModel * vec4(aPos, 1.0);
    vColor = aInstanceColor;
}
)glsl";

static const char* FRAGMENT_SHADER_SRC = R"glsl(
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)glsl";

GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error compilando shader: %s\n", log);
        std::exit(EXIT_FAILURE);
    }
    return shader;
}

GLuint buildShaderProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, VERTEX_SHADER_SRC);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error enlazando shader program: %s\n", log);
        std::exit(EXIT_FAILURE);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// Buffers globales de instancia, reconstruidos cada frame a partir de terrain.
struct InstanceData {
    glm::mat4 model;
    glm::vec3 color;
};
std::vector<InstanceData> instanceBuffer;

// Renderiza todos los bloques activos de terrain con un solo draw call para N bloques.
void renderTerrain(GLuint vao, GLuint instanceVBO, GLuint shaderProgram, const glm::mat4& viewProj) {
    instanceBuffer.clear();
    instanceBuffer.reserve(terrain.size());
    for (const auto& b : terrain) {
        InstanceData inst;
        inst.model = glm::translate(glm::mat4(1.0f), b.pos);
        inst.color = b.color;
        instanceBuffer.push_back(inst);
    }

    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, instanceBuffer.size() * sizeof(InstanceData),
                 instanceBuffer.data(), GL_DYNAMIC_DRAW);

    glUseProgram(shaderProgram);
    GLint loc = glGetUniformLocation(shaderProgram, "uViewProj");
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(viewProj));

    glBindVertexArray(vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(instanceBuffer.size()));
    glBindVertexArray(0);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Uso: %s <N>\n  N = cantidad de bloques del terreno (entero positivo)\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* endPtr = nullptr;
    long n = std::strtol(argv[1], &endPtr, 10);
    if (endPtr == argv[1] || *endPtr != '\0' || n <= 0) {
        std::fprintf(stderr, "Error: N debe ser un entero positivo. Valor recibido: '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    // Inicializacion de ventana y contexto OpenGL
    if (!glfwInit()) {
        std::fprintf(stderr, "Error: no se pudo inicializar GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                           "Minecraft Screen Saver - Paralelo", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Error: no se pudo crear la ventana GLFW\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        std::fprintf(stderr, "Error: no se pudo inicializar GLEW: %s\n", glewGetErrorString(glewStatus));
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f); // celeste tipo "cielo"

    // Almacenamiento de los N elementos a renderizar
    initTerrain(static_cast<int>(n));

    // Geometria del cubo + buffer de instancias
    GLuint vao, vbo, instanceVBO;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &instanceVBO);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTICES), CUBE_VERTICES, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Atributo de instancia: mat4 (4 vec4 consecutivos, locations 1..4) + color (location 5)
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    for (int i = 0; i < 4; ++i) {
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                               (void*)(sizeof(glm::vec4) * i));
        glEnableVertexAttribArray(1 + i);
        glVertexAttribDivisor(1 + i, 1);
    }
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                           (void*)offsetof(InstanceData, color));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    glBindVertexArray(0);

    GLuint shaderProgram = buildShaderProgram();

    // Camara fija mirando hacia la grilla de bloques
    glm::mat4 projection = glm::perspective(glm::radians(60.0f),
                                             static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT,
                                             0.1f, 200.0f);

    // Loop principal
    double lastTime = glfwGetTime();
    double fpsTimer = lastTime;
    int frameCount = 0;

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float dt = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        glfwPollEvents();

        updateTerrain(dt);

        double camAngle = currentTime * 0.15;
        glm::vec3 camPos(std::cos(camAngle) * 25.0, 18.0, std::sin(camAngle) * 25.0);
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 viewProj = projection * view;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderTerrain(vao, instanceVBO, shaderProgram, viewProj);

        glfwSwapBuffers(window);

        // FPS en el titulo de la ventana
        frameCount++;
        if (currentTime - fpsTimer >= 1.0) {
            char title[128];
            std::snprintf(title, sizeof(title),
                           "Minecraft Screen Saver - Paralelo | N=%ld | Hilos disponibles: %d | FPS: %d",
                           n, omp_get_max_threads(), frameCount);
            glfwSetWindowTitle(window, title);
            frameCount = 0;
            fpsTimer = currentTime;
        }
    }

    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &instanceVBO);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
