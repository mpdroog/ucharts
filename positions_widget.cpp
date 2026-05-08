// positions_widget.cpp - Positions display implementation
#include "positions_widget.h"
#include "chart_widget.h"  // For make_color helper
#include <cstdio>

PositionsWidget::PositionsWidget()
    : m_order_mgr(nullptr)
{
}

void PositionsWidget::set_order_manager(OrderManager* order_mgr) {
    m_order_mgr = order_mgr;
}

void PositionsWidget::render_pnl(float pnl, float pnl_percent) {
    ImU32 color = (pnl >= 0.0f) ? make_color(0, 255, 0, 255) : make_color(255, 0, 0, 255);
    const char* sign = (pnl >= 0.0f) ? "+" : "";

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::Text("%s%.2f", sign, static_cast<double>(pnl));
    ImGui::NextColumn();
    ImGui::Text("%s%.1f%%", sign, static_cast<double>(pnl_percent));
    ImGui::PopStyleColor();
}

void PositionsWidget::render_open_positions(ImVec2 size) {
    ImGui::TextUnformatted("Open Positions");
    ImGui::Separator();

    if (ImGui::BeginChild("OpenPositions", ImVec2(size.x, size.y * 0.6f), false)) {
        // Column headers
        ImGui::PushStyleColor(ImGuiCol_Text, make_color(150, 150, 150, 255));
        float col_width = size.x / 6.0f;
        ImGui::Columns(6, "OpenPosHeader", false);
        ImGui::SetColumnWidth(0, col_width);
        ImGui::SetColumnWidth(1, col_width * 0.8f);
        ImGui::SetColumnWidth(2, col_width);
        ImGui::SetColumnWidth(3, col_width);
        ImGui::SetColumnWidth(4, col_width);
        ImGui::SetColumnWidth(5, col_width);

        ImGui::Text("Symbol");
        ImGui::NextColumn();
        ImGui::Text("Qty");
        ImGui::NextColumn();
        ImGui::Text("Avg");
        ImGui::NextColumn();
        ImGui::Text("Cur");
        ImGui::NextColumn();
        ImGui::Text("P&L");
        ImGui::NextColumn();
        ImGui::Text("P&L%%");
        ImGui::NextColumn();
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Position rows
        if (m_order_mgr != nullptr) {
            const std::vector<Position>& positions = m_order_mgr->get_open_positions();
            for (const auto& pos : positions) {
                // Calculate P&L
                float cost_basis = pos.avg_price * static_cast<float>(pos.quantity);
                float current_value = pos.current_price * static_cast<float>(pos.quantity);
                float pnl = current_value - cost_basis;
                float pnl_percent = (cost_basis > 0.0f) ? (pnl / cost_basis) * 100.0f : 0.0f;

                ImGui::Text("%s", pos.symbol);
                ImGui::NextColumn();
                ImGui::Text("%d", pos.quantity);
                ImGui::NextColumn();
                ImGui::Text("%.2f", static_cast<double>(pos.avg_price));
                ImGui::NextColumn();
                ImGui::Text("%.2f", static_cast<double>(pos.current_price));
                ImGui::NextColumn();
                render_pnl(pnl, pnl_percent);
                ImGui::NextColumn();
            }
        }

        ImGui::Columns(1);
    }
    ImGui::EndChild();

    // Pending orders section
    ImGui::Spacing();
    ImGui::TextUnformatted("Pending Orders");
    ImGui::Separator();

    if (ImGui::BeginChild("PendingOrders", ImVec2(size.x, size.y * 0.35f), false)) {
        // Column headers
        ImGui::PushStyleColor(ImGuiCol_Text, make_color(150, 150, 150, 255));
        float col_width = size.x / 5.0f;
        ImGui::Columns(5, "PendingHeader", false);
        ImGui::SetColumnWidth(0, col_width);
        ImGui::SetColumnWidth(1, col_width * 0.6f);
        ImGui::SetColumnWidth(2, col_width * 0.8f);
        ImGui::SetColumnWidth(3, col_width);
        ImGui::SetColumnWidth(4, col_width * 0.5f);

        ImGui::Text("Symbol");
        ImGui::NextColumn();
        ImGui::Text("Side");
        ImGui::NextColumn();
        ImGui::Text("Qty");
        ImGui::NextColumn();
        ImGui::Text("Price");
        ImGui::NextColumn();
        ImGui::Text("");  // Cancel column
        ImGui::NextColumn();
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Order rows
        if (m_order_mgr != nullptr) {
            const std::vector<Order>& orders = m_order_mgr->get_pending_orders();
            int cancel_id = -1;

            for (const auto& order : orders) {
                ImGui::Text("%s", order.symbol);
                ImGui::NextColumn();

                // Side with color
                if (order.side == SIDE_BUY) {
                    ImGui::PushStyleColor(ImGuiCol_Text, make_color(0, 255, 0, 255));
                    ImGui::Text("BUY");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, make_color(255, 0, 0, 255));
                    ImGui::Text("SELL");
                    ImGui::PopStyleColor();
                }
                ImGui::NextColumn();

                ImGui::Text("%d", order.quantity);
                ImGui::NextColumn();
                ImGui::Text("%.2f", static_cast<double>(order.price));
                ImGui::NextColumn();

                // Cancel button (red X)
                ImGui::PushID(static_cast<int>(order.id));
                ImGui::PushStyleColor(ImGuiCol_Button, make_color(150, 0, 0, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, make_color(200, 0, 0, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, make_color(255, 0, 0, 255));

                if (ImGui::SmallButton("X")) {
                    cancel_id = static_cast<int>(order.id);
                }

                ImGui::PopStyleColor(3);
                ImGui::PopID();
                ImGui::NextColumn();
            }

            // Process cancel after iteration
            if (cancel_id >= 0) {
                m_order_mgr->cancel_order(static_cast<int64_t>(cancel_id));
            }
        }

        ImGui::Columns(1);
    }
    ImGui::EndChild();
}

void PositionsWidget::render_closed_positions(ImVec2 size) {
    ImGui::TextUnformatted("Closed Positions");
    ImGui::Separator();

    if (ImGui::BeginChild("ClosedPositions", size, false)) {
        // Column headers
        ImGui::PushStyleColor(ImGuiCol_Text, make_color(150, 150, 150, 255));
        float col_width = size.x / 4.0f;
        ImGui::Columns(4, "ClosedHeader", false);
        ImGui::SetColumnWidth(0, col_width);
        ImGui::SetColumnWidth(1, col_width);
        ImGui::SetColumnWidth(2, col_width);
        ImGui::SetColumnWidth(3, col_width);

        ImGui::Text("Symbol");
        ImGui::NextColumn();
        ImGui::Text("Entry");
        ImGui::NextColumn();
        ImGui::Text("Exit");
        ImGui::NextColumn();
        ImGui::Text("P&L");
        ImGui::NextColumn();
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Closed position rows
        if (m_order_mgr != nullptr) {
            const std::vector<ClosedPosition>& closed = m_order_mgr->get_closed_positions();
            for (const auto& pos : closed) {
                float pnl = (pos.exit_price - pos.entry_price) * static_cast<float>(pos.quantity);

                ImGui::Text("%s", pos.symbol);
                ImGui::NextColumn();
                ImGui::Text("%.2f", static_cast<double>(pos.entry_price));
                ImGui::NextColumn();
                ImGui::Text("%.2f", static_cast<double>(pos.exit_price));
                ImGui::NextColumn();

                // P&L with color
                ImU32 color = (pnl >= 0.0f) ? make_color(0, 255, 0, 255) : make_color(255, 0, 0, 255);
                const char* sign = (pnl >= 0.0f) ? "+" : "";

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Text("%s%.2f", sign, static_cast<double>(pnl));
                ImGui::PopStyleColor();
                ImGui::NextColumn();
            }
        }

        ImGui::Columns(1);
    }
    ImGui::EndChild();
}
