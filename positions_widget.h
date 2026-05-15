// positions_widget.h - Open and closed positions display
#ifndef POSITIONS_WIDGET_H
#define POSITIONS_WIDGET_H

#include "imgui.h"
#include "types.h"
#include "order_manager.h"
#include <vector>

// Widget for displaying open positions, pending orders, and closed positions
class PositionsWidget {
public:
    PositionsWidget();

    // Set order manager reference
    void set_order_manager(OrderManager* order_mgr);
    void set_route_getter(RouteGetter getter);

    // Render open positions panel
    void render_open_positions(ImVec2 size);

    // Render closed positions panel
    void render_closed_positions(ImVec2 size);

private:
    OrderManager* m_order_mgr;
    RouteGetter m_route_getter;

    // Helper to format P&L
    void render_pnl(float pnl, float pnl_percent);
};

#endif // POSITIONS_WIDGET_H
