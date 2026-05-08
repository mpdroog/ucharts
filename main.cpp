// Simple Candlestick Chart Application
// Uses Dear ImGui with GLFW + OpenGL3 backend

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// OHLC data structure
struct Candle {
    float open;
    float high;
    float low;
    float close;
};

// Global data
static std::vector<Candle> g_candles;
static char g_csv_path[256] = "sample.csv";

// Parse CSV file with open,high,low,close format
static bool LoadCSV(const char* filename) {
    FILE* file = std::fopen(filename, "r");
    if (file == nullptr) {
        std::fprintf(stderr, "Failed to open file: %s\n", filename);
        return false;
    }

    g_candles.clear();
    char line[256];
    int line_num = 0;

    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        line_num++;

        // Skip header line if it contains non-numeric data
        if (line_num == 1 && (std::strstr(line, "open") != nullptr ||
                              std::strstr(line, "Open") != nullptr ||
                              std::strstr(line, "OPEN") != nullptr)) {
            continue;
        }

        Candle c = {0.0f, 0.0f, 0.0f, 0.0f};
        if (std::sscanf(line, "%f,%f,%f,%f", &c.open, &c.high, &c.low, &c.close) == 4) {
            g_candles.push_back(c);
        }
    }

    std::fclose(file);
    std::printf("Loaded %zu candles from %s\n", g_candles.size(), filename);
    return !g_candles.empty();
}

// Draw candlestick chart
static void DrawCandlestickChart(ImVec2 size) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    if (canvas_size.x <= 0.0f) {
        canvas_size.x = ImGui::GetContentRegionAvail().x;
    }
    if (canvas_size.y <= 0.0f) {
        canvas_size.y = 300.0f;
    }

    // Draw background
    draw_list->AddRectFilled(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(30, 30, 30, 255));
    draw_list->AddRect(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(100, 100, 100, 255));

    if (g_candles.empty()) {
        ImGui::Dummy(canvas_size);
        return;
    }

    // Find price range
    float min_price = g_candles[0].low;
    float max_price = g_candles[0].high;
    for (const auto& c : g_candles) {
        if (c.low < min_price) {
            min_price = c.low;
        }
        if (c.high > max_price) {
            max_price = c.high;
        }
    }

    // Add padding to price range
    float price_range = max_price - min_price;
    min_price -= price_range * 0.05f;
    max_price += price_range * 0.05f;
    price_range = max_price - min_price;

    // Calculate dimensions
    const float padding = 20.0f;
    const float chart_width = canvas_size.x - padding * 2.0f;
    const float chart_height = canvas_size.y - padding * 2.0f;
    const float candle_width = chart_width / static_cast<float>(g_candles.size());
    const float body_width = candle_width * 0.7f;

    // Helper to convert price to Y coordinate
    auto priceToY = [&](float price) -> float {
        return canvas_pos.y + padding + chart_height - ((price - min_price) / price_range * chart_height);
    };

    // Draw grid lines
    const int num_grid_lines = 5;
    for (int i = 0; i <= num_grid_lines; i++) {
        float price = min_price + (price_range * static_cast<float>(i) / static_cast<float>(num_grid_lines));
        float y = priceToY(price);
        draw_list->AddLine(
            ImVec2(canvas_pos.x + padding, y),
            ImVec2(canvas_pos.x + canvas_size.x - padding, y),
            IM_COL32(60, 60, 60, 255));

        // Price label
        char price_label[32];
        std::snprintf(price_label, sizeof(price_label), "%.2f", static_cast<double>(price));
        draw_list->AddText(ImVec2(canvas_pos.x + 2.0f, y - 6.0f), IM_COL32(150, 150, 150, 255), price_label);
    }

    // Draw candles
    for (std::size_t i = 0; i < g_candles.size(); i++) {
        const Candle& c = g_candles[i];
        float x = canvas_pos.x + padding + static_cast<float>(i) * candle_width + candle_width / 2.0f;

        float open_y = priceToY(c.open);
        float close_y = priceToY(c.close);
        float high_y = priceToY(c.high);
        float low_y = priceToY(c.low);

        bool bullish = c.close >= c.open;
        ImU32 color = bullish ? IM_COL32(0, 200, 100, 255) : IM_COL32(220, 60, 60, 255);

        // Draw wick (high-low line)
        draw_list->AddLine(
            ImVec2(x, high_y),
            ImVec2(x, low_y),
            color, 1.0f);

        // Draw body
        float body_top = bullish ? close_y : open_y;
        float body_bottom = bullish ? open_y : close_y;

        if (body_bottom - body_top < 1.0f) {
            // Doji - just draw a line
            draw_list->AddLine(
                ImVec2(x - body_width / 2.0f, open_y),
                ImVec2(x + body_width / 2.0f, open_y),
                color, 2.0f);
        } else {
            draw_list->AddRectFilled(
                ImVec2(x - body_width / 2.0f, body_top),
                ImVec2(x + body_width / 2.0f, body_bottom),
                color);
        }
    }

    // Reserve space for the canvas
    ImGui::Dummy(canvas_size);
}

static void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int argc, char** argv) {
    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() == GLFW_FALSE) {
        return 1;
    }

    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    // Create window
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Candlestick Chart", nullptr, nullptr);
    if (window == nullptr) {
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load initial CSV if provided as argument or default
    if (argc > 1) {
        std::strncpy(g_csv_path, argv[1], sizeof(g_csv_path) - 1);
        g_csv_path[sizeof(g_csv_path) - 1] = '\0';
    }
    LoadCSV(g_csv_path);

    // Main loop
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Main window
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Candlestick Chart", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse);

        // Controls
        ImGui::Text("CSV File:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##csvpath", g_csv_path, sizeof(g_csv_path));
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            LoadCSV(g_csv_path);
        }

        ImGui::Separator();

        // Status
        ImGui::Text("Loaded: %zu candles", g_candles.size());
        if (!g_candles.empty()) {
            ImGui::SameLine();
            ImGui::Text(" | First: O=%.2f H=%.2f L=%.2f C=%.2f",
                static_cast<double>(g_candles[0].open),
                static_cast<double>(g_candles[0].high),
                static_cast<double>(g_candles[0].low),
                static_cast<double>(g_candles[0].close));
        }

        ImGui::Separator();

        // Draw the chart
        DrawCandlestickChart(ImVec2(0, 0));

        ImGui::End();

        // Rendering
        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
