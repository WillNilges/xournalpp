/*
 * Xournal++
 *
 * Lua API, application library
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <cstring>
#include <map>

#include <gtk/gtk.h>
#include <stdint.h>

#include "control/Control.h"
#include "control/PageBackgroundChangeController.h"
#include "control/Tool.h"
#include "control/layer/LayerController.h"
#include "control/pagetype/PageTypeHandler.h"
#include "gui/XournalView.h"
#include "gui/widgets/XournalWidget.h"
#include "model/Font.h"
#include "model/SplineSegment.h"
#include "model/StrokeStyle.h"
#include "model/Text.h"
#include "util/StringUtils.h"
#include "util/XojMsgBox.h"
#include "util/safe_casts.h"

/**
 * Renames file 'from' to file 'to' in the file system.
 * Overwrites 'to' if it already exists.
 *
 * Example:
 *   assert(app.glib_rename("path/to/foo", "other/bar"))
 *
 * Preferred to os.rename() because it works across
 * partitions. Uses glib's rename function.
 *
 * Returns 1 on success, and (nil, message) on failure.
 */
static int applib_glib_rename(lua_State* L) {
    GError* err = nullptr;
    GFile* to = g_file_new_for_path(lua_tostring(L, -1));
    GFile* from = g_file_new_for_path(lua_tostring(L, -2));

    g_file_move(from, to, G_FILE_COPY_OVERWRITE, nullptr, nullptr, nullptr, &err);
    g_object_unref(from);
    g_object_unref(to);
    if (err) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s (error code: %d)", err->message, err->code);
        g_error_free(err);
        return 2;
    } else {
        lua_pushinteger(L, 1);
        return 1;
    }
}


/**
 * Create a 'Save As' native dialog and return as a string
 * the filepath of the location the user chose to save.
 *
 * Examples:
 *   local filename = app.saveAs() -- defaults to suggestion "Untitled"
 *   local filename = app.saveAs("foo") -- suggests "foo" as filename
 */
static int applib_saveAs(lua_State* L) {
    GtkFileChooserNative* native;
    gint res;
    int args_returned = 0;  // change to 1 if user chooses file

    const char* filename = luaL_checkstring(L, -1);

    // Create a 'Save As' native dialog
    native = gtk_file_chooser_native_new(_("Save file"), nullptr, GTK_FILE_CHOOSER_ACTION_SAVE, nullptr, nullptr);

    // If user tries to overwrite a file, ask if it's OK
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);
    // Offer a suggestion for the filename if filename absent
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native),
                                      filename ? filename : (std::string{_("Untitled")}).c_str());

    // Wait until user responds to dialog
    res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));

    // Return the filename chosen to lua
    if (res == GTK_RESPONSE_ACCEPT) {
        char* filename = static_cast<char*>(gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native)));

        lua_pushlstring(L, filename, strlen(filename));
        g_free(static_cast<gchar*>(filename));
        args_returned = 1;
    }

    // Destroy the dialog and free memory
    g_object_unref(native);

    return args_returned;
}

/**
 * Create a 'Open File' native dialog and return as a string
 * the filepath the user chose to open.
 *
 * Examples:
 *   path = app.getFilePath()
 *   path = app.getFilePath({'*.bmp', '*.png'})
 */
static int applib_getFilePath(lua_State* L) {
    GtkFileChooserNative* native =
            gtk_file_chooser_native_new(_("Open file"), nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, nullptr, nullptr);
    gint res;
    int args_returned = 0;  // change to 1 if user chooses file
    char* filename;

    // Get vector of supported formats from Lua stack
    std::vector<std::string> formats;
    // stack now contains: -1 => table
    lua_pushnil(L);
    // stack now contains: -1 => nil; -2 => table
    while (lua_next(L, -2)) {
        // stack now contains: -1 => value; -2 => key; -3 => table
        const char* value = lua_tostring(L, -1);
        formats.push_back(value);
        lua_pop(L, 1);
        // stack now contains: -1 => key; -2 => table
    }
    // stack now contains: -1 => table
    lua_pop(L, 1);  // Stack is now the same as it was on entry to this function
    if (formats.size() > 0) {
        GtkFileFilter* filterSupported = gtk_file_filter_new();
        gtk_file_filter_set_name(filterSupported, _("Supported files"));
        for (std::string format: formats) gtk_file_filter_add_pattern(filterSupported, format.c_str());
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filterSupported);
    }

    // Wait until user responds to dialog
    res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    // Return the filename chosen to lua
    if (res == GTK_RESPONSE_ACCEPT) {
        filename = static_cast<char*>(gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native)));
        lua_pushlstring(L, filename, strlen(filename));
        g_free(static_cast<gchar*>(filename));
        args_returned = 1;
    }
    // Destroy the dialog and free memory
    g_object_unref(native);
    return args_returned;
}

/**
 * Example: local result = app.msgbox("Test123", {[1] = "Yes", [2] = "No"})
 * Pops up a message box with two buttons "Yes" and "No" and returns 1 for yes, 2 for no
 */
static int applib_msgbox(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);

    // discard any extra arguments passed in
    lua_settop(L, 2);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_pushnil(L);

    std::map<int, std::string> button;

    while (lua_next(L, 2) != 0) {
        int index = lua_tointeger(L, -2);
        const char* buttonText = luaL_checkstring(L, -1);
        lua_pop(L, 1);

        button.insert(button.begin(), std::pair<int, std::string>(index, buttonText));
    }

    Plugin* plugin = Plugin::getPluginFromLua(L);

    int result = XojMsgBox::showPluginMessage(plugin->getName(), msg, button);
    lua_pushinteger(L, result);
    return 1;
}


/**
 * Allow to register menupoints, this needs to be called from initUi
 *
 * Example: app.registerUi({["menu"] = "HelloWorld", callback="printMessage", accelerator="<Control>a"})
 * registers a menupoint with name "HelloWorld" executing a function named "printMessage",
 * which can be triggered via the "<Control>a" keyboard accelerator
 */
static int applib_registerUi(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    if (!plugin->isInInitUi()) {
        luaL_error(L, "registerUi needs to be called within initUi()");
    }

    // discard any extra arguments passed in
    lua_settop(L, 1);
    luaL_checktype(L, 1, LUA_TTABLE);

    // Now to get the data out of the table
    // 'unpack' the table by putting the values onto
    // the stack first. Then convert those stack values
    // into an appropriate C type.
    lua_getfield(L, 1, "accelerator");
    lua_getfield(L, 1, "menu");
    lua_getfield(L, 1, "callback");
    // stack now has following:
    //    1 = {"menu"="MenuName", callback="functionName", accelerator="<Control>a"}
    //   -3 = "<Control>a"
    //   -2 = "MenuName"
    //   -1 = "functionName"

    const char* accelerator = luaL_optstring(L, -3, nullptr);
    const char* menu = luaL_optstring(L, -2, nullptr);
    const char* callback = luaL_optstring(L, -1, nullptr);
    if (callback == nullptr) {
        luaL_error(L, "Missing callback function!");
    }
    if (menu == nullptr) {
        menu = "";
    }
    if (accelerator == nullptr) {
        accelerator = "";
    }

    int toolbarId = -1;

    int menuId = plugin->registerMenu(menu, callback, accelerator);

    // Make sure to remove all vars which are put to the stack before!
    lua_pop(L, 3);

    // Add return value to the Stack
    lua_createtable(L, 0, 2);

    lua_pushstring(L, "menuId");
    lua_pushinteger(L, menuId);
    lua_settable(L, -3); /* 3rd element from the stack top */

    lua_pushstring(L, "toolbarId");
    lua_pushinteger(L, toolbarId);
    lua_settable(L, -3);

    return 1;
}

/**
 * Execute an UI action (usually internally called from Toolbar / Menu)
 * The argument consists of a Lua table with 3 keys: "action", "group" and "enabled"
 * The key "group" is currently only used for debugging purpose and can safely be omitted.
 * The key "enabled" is true by default.
 *
 * Example 1: app.uiAction({["action"] = "ACTION_PASTE"})
 * pastes the clipboard content into the document
 *
 * Example 2: app.uiAction({["action"] = "ACTION_TOOL_DRAW_ELLIPSE", ["enabled"] = false})
 * turns off the Ellipse drawing type
 */
static int applib_uiAction(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);

    // discard any extra arguments passed in
    lua_settop(L, 1);
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "group");
    lua_getfield(L, 1, "enabled");
    lua_getfield(L, 1, "action");
    // stack now has following:
    //    1 = {["action"] = "ACTION_GRID_SNAPPING", ["group"] = "GROUP_GRID_SNAPPING", ["enabled"] = true}
    //   -3 = GROUP_GRID_SNAPPING
    //   -2 = true
    //   -1 = "ACTION_GRID_SNAPPING"

    bool enabled = true;

    ActionGroup group = GROUP_NOGROUP;
    const char* groupStr = luaL_optstring(L, -3, nullptr);
    if (groupStr != nullptr) {
        group = ActionGroup_fromString(groupStr);
    }

    if (lua_isboolean(L, -2)) {
        enabled = lua_toboolean(L, -2);
    }

    const char* actionStr = luaL_optstring(L, -1, nullptr);
    if (actionStr == nullptr) {
        luaL_error(L, "Missing action!");
    }

    ActionType action = ActionType_fromString(actionStr);
    GdkEvent* event = nullptr;
    GtkMenuItem* menuitem = nullptr;
    GtkToolButton* toolbutton = nullptr;

    Control* ctrl = plugin->getControl();
    ctrl->actionPerformed(action, group, event, menuitem, toolbutton, enabled);

    // Make sure to remove all vars which are put to the stack before!
    lua_pop(L, 3);

    return 1;
}

/**
 * Select UI action (notifies action listeners)
 * Unless you are sure what you do, use app.uiAction instead!
 * The problem is that only notifying action listeners does not store these changes in the settings, which may create
 * confusion
 *
 * Example: app.uiActionSelected("GROUP_GRID_SNAPPING", "ACTION_GRID_SNAPPING")
 * notifies the action listeners that grid snapping is turned on; it is not recorded in the settings,
 * so better use app.uiAction({["action"] = "ACTION_GRID_SNAPPING") instead
 */
static int applib_uiActionSelected(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);

    ActionGroup group = ActionGroup_fromString(luaL_checkstring(L, 1));
    ActionType action = ActionType_fromString(luaL_checkstring(L, 2));

    Control* ctrl = plugin->getControl();
    ctrl->fireActionSelected(group, action);

    return 1;
}

/**
 * Execute action from sidebar menu
 *
 * Example: app.sidebarAction("MOVE_DOWN")
 * moves down the current page
 */
static int applib_sidebarAction(lua_State* L) {
    // Connect the context menu actions
    const std::map<std::string, SidebarActions> actionMap = {
            {"COPY", SIDEBAR_ACTION_COPY},
            {"DELETE", SIDEBAR_ACTION_DELETE},
            {"MOVE_UP", SIDEBAR_ACTION_MOVE_UP},
            {"MOVE_DOWN", SIDEBAR_ACTION_MOVE_DOWN},
            {"NEW_BEFORE", SIDEBAR_ACTION_NEW_BEFORE},
            {"NEW_AFTER", SIDEBAR_ACTION_NEW_AFTER},
    };
    const char* actionStr = luaL_checkstring(L, 1);
    if (actionStr == nullptr) {
        luaL_error(L, "Missing action!");
    }
    auto pos = actionMap.find(actionStr);
    if (pos == actionMap.end()) {
        luaL_error(L, "Unkonwn action: %s", actionStr);
    }
    Plugin* plugin = Plugin::getPluginFromLua(L);
    SidebarToolbar* toolbar = plugin->getControl()->getSidebar()->getToolbar();
    toolbar->runAction(pos->second);

    return 1;
}

/**
 * Execute action from layer controller
 *
 * Example: app.layerAction("ACTION_DELETE_LAYER")
 * deletes the current layer
 */
static int applib_layerAction(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);

    ActionType action = ActionType_fromString(luaL_checkstring(L, 1));

    Control* ctrl = plugin->getControl();
    ctrl->getLayerController()->actionPerformed(action);

    return 1;
}

/**
 * Helper function for addStroke API. Parses pen settings from API call, taking
 * in a Stroke and a chosen Layer, sets the pen settings, and applies the stroke.
 */
static void addStrokeHelper(lua_State* L, Stroke* stroke, Layer* layer) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* ctrl = plugin->getControl();

    std::string size;
    double thickness;
    int fillOpacity;
    bool filled;
    //Tool& tool;
    Color color;
    std::string drawingType;
    std::string lineStyle;
    ToolHandler* toolHandler;
    const char* tool;

    // discard any extra arguments passed in
    lua_settop(L, 1);
    luaL_checktype(L, 1, LUA_TTABLE);

    // Make sure we have enough points to form a stroke
    if (stroke->getPointCount() >= 2) {

        // Get attributes.
        lua_getfield(L, 1, "tool");
        lua_getfield(L, 1, "width");
        lua_getfield(L, 1, "color");
        lua_getfield(L, 1, "fill");
        lua_getfield(L, 1, "lineStyle");

        tool = luaL_optstring(L, -5, ""); // First thing, we're gonna need the tool type.

        toolHandler = ctrl->getToolHandler();

        // TODO: (willnilges) Handle DrawingType?
        // TODO: (willnilges) Break out Eraser functionality into a new API call.

        // Set tool type
        if (strcmp("highlighter", tool) == 0) {
            stroke->setToolType(STROKE_TOOL_HIGHLIGHTER);

            size = toolSizeToString(toolHandler->getHighlighterSize());
            thickness = toolHandler->getToolThickness(TOOL_HIGHLIGHTER)[toolSizeFromString(size)];

            fillOpacity = toolHandler->getHighlighterFill();
            filled = toolHandler->getHighlighterFillEnabled();

            Tool& tool = toolHandler->getTool(TOOL_HIGHLIGHTER);
            color = tool.getColor();
            std::string drawingType = drawingTypeToString(tool.getDrawingType());
        } else { // Tool type must be pen
            if (!(strcmp("pen", tool) == 0))
                g_warning("%s", FC(_F("Unknown stroke type: \"{1}\", defaulting to pen") % tool));

            stroke->setToolType(STROKE_TOOL_PEN);

            size         = toolSizeToString(toolHandler->getPenSize());
            thickness    = toolHandler->getToolThickness(TOOL_PEN)[toolSizeFromString(size)];

            fillOpacity  = toolHandler->getPenFill();
            filled       = toolHandler->getPenFillEnabled();

            Tool& tool   = toolHandler->getTool(TOOL_PEN);
            color        = tool.getColor();
            drawingType  = drawingTypeToString(tool.getDrawingType());
            lineStyle    = StrokeStyle::formatStyle(tool.getLineStyle());
        }

        // Set width
        if (lua_isnumber(L, -4)) // Check if the width was provided
            stroke->setWidth(lua_tonumber(L, -4));
        else
            stroke->setWidth(thickness);

        // Set color
        if (lua_isinteger(L, -3)) // Check if the color was provided
            stroke->setColor(Color(lua_tointeger(L, -3)));
        else
            stroke->setColor(color);

        // Set fill
        if (lua_isinteger(L, -2)) // Check if fill settings were provided
            stroke->setFill(lua_tointeger(L, -2));
        else if (filled)
            stroke->setFill(fillOpacity);
        else
            stroke->setFill(-1); // No fill

        // Set line style
        if (lua_isstring(L, -1)) // Check if line style settings were provided
            stroke->setLineStyle(StrokeStyle::parseStyle(lua_tostring(L, -1)));
        else
            stroke->setLineStyle(StrokeStyle::parseStyle(lineStyle.data()));

        lua_pop(L, 5); // Finally done with all that Lua data.

        // Add the Stroke
        layer->addElement(stroke);
        stroke = nullptr;
        return;
    }
    // If there aren't at least 2 points, then don't add the stroke.
    g_warning("Stroke shorter than two points. Discarding. (Has %d)", stroke->getPointCount());
    delete stroke;
    stroke = nullptr;
    return;
}

/**
 * Given a table containing a series of splines, draws a stroke on the canvas.
 * Expects a table of coordinate pairs along with attributes of the stroke.
 * Attributes are optional.
 *
 * The function expects 8 points per spline. Due to the nature of quadratic
 * splines, you must pass your points in a repeating pattern:
 * startX, startY, ctrl1X, ctrl1Y, ctrl2X, ctrl2Y, endX, endY, startX, startY, ...
 *
 * The function checks that the spline table is divisible by eight, and will throw
 * an error if it is not.
 *
 * Example: app.addStroke({
 *            ["splines"] = {
 *              [1] = 880.0,
 *              [2] = 874.0,
 *              [3] = 881.3295,
 *              [4] = 851.5736,
 *              [5] = 877.2915,
 *              [6] = 828.2946,
 *              [7] = 875.1697,
 *              [8] = 806.0,
 *              [...] = ...,
 *            },
 *            ["width"] = 1.4,
 *            ["color"] = 0xff000000,
 *            ["fill"] = 0,
 *            ["tool"] = "pen",
 *            ["lineStyle"] = "solid"
 *        })
 */
static int applib_addSpline(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* ctrl = plugin->getControl();
    PageRef const& page = ctrl->getCurrentPage();
    Layer* layer = page->getSelectedLayer();
    Stroke* stroke = new Stroke();

    // Discard any extra arguments passed in
    lua_settop(L, 1);
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "splines");
    if (!lua_istable(L, -2))
        luaL_error(L, "Missing Spline table!");
    // stack now contains: -1 => table
    lua_pushnil(L);
    // stack now contains: -1 => nil; -2 => table
    std::vector<double> coordStream;
    while (lua_next(L, -2)) {
        // stack now contains: -1 => value; -2 => key; -3 => table
        double value = lua_tonumber(L, -1);
        coordStream.push_back(value);
        // pop value + copy of key, leaving original key
        lua_pop(L, 1);
        // stack now contains: -1 => key; -2 => table
    }
    // stack now contains: -1 => table
    lua_pop(L, 1);  // Stack is now the same as it was on entry to this function

    // Check if the list is divisible by 8.
    if (coordStream.size() % 8 != 0)
        luaL_error(L, "Spline table incomplete!");

    // Now take that gigantic list of splines and create SplineSegments out of them.
    long unsigned int i = 0;
    while (i < coordStream.size()) {
        // start, ctrl1, ctrl2, end
        Point start = Point(coordStream.at(i), coordStream.at(i + 1), Point::NO_PRESSURE);
        Point ctrl1 = Point(coordStream.at(i + 2), coordStream.at(i + 3), Point::NO_PRESSURE);
        Point ctrl2 = Point(coordStream.at(i + 4), coordStream.at(i + 5), Point::NO_PRESSURE);
        Point end = Point(coordStream.at(i + 6), coordStream.at(i + 7), Point::NO_PRESSURE);
        i += 8;
        SplineSegment segment = SplineSegment(start, ctrl1, ctrl2, end);
        std::list<Point> raster = segment.toPointSequence();
        for (Point point: raster) stroke->addPoint(point);
        // TODO: (willnilges) Is there a way we can get Pressure with Splines?
    }
    // Finish building the Stroke and apply it to the layer.
    addStrokeHelper(L, stroke, layer);
    return 0;
}

/**
 * Given a set of points, draws a stroke on the canvas.
 * Expects three tables of equal length: one for X, one for Y, and one for
 * stroke pressure, along with attributes of the stroke.
 * Attributes are optional. Pressure is optional.
 *
 * The function checks for consistency among table lengths, and throws an
 * error if there is a discrepancy
 *
 * Example: app.addStroke({
 *            ["x"] = {
 *              [1] = 101.0,
 *              [2] = 102.0,
 *              [...] = ...,
 *            },
 *            ["y"] = {
 *              [1] = 100.0,
 *              [2] = 100.0,
 *              [...] = ...,
 *            },
 *            ["pressure"] = {
 *              [1] = 0.5,
 *              [2] = 0.4,
 *              [...] = ...,
 *            },
 *            ["width"] = 1.4,
 *            ["color"] = 0xff0000,
 *            ["fill"] = 0,
 *            ["tool"] = "STROKE_TOOL_PEN",
 *            ["lineStyle"] = "default"
 *        })
 */
static int applib_addStroke(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* ctrl = plugin->getControl();
    PageRef const& page = ctrl->getCurrentPage();
    Layer* layer = page->getSelectedLayer();
    Stroke* stroke = new Stroke();

    std::vector<double> xStream;
    std::vector<double> yStream;
    std::vector<double> pressureStream;

    // Discard any extra arguments passed in
    lua_settop(L, 1);
    luaL_checktype(L, 1, LUA_TTABLE);
    // stack now contains: -1 => table
    lua_pushnil(L);
    // stack now contains: -1 => nil; -2 => table
    // Fetch table of X values from the Lua stack
    lua_getfield(L, 1, "x");  // X coords are now at -1
    lua_pushnil(L);           // X coords are now at -2
    if (!lua_istable(L, -2))
        luaL_error(L, "Missing X-Coordinate table!");
    while (lua_next(L, -2)) {
        double value = lua_tonumber(L, -1);
        xStream.push_back(value);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  // Stack is back to normal

    // Fetch table of Y values form the Lua stack
    // (same drill as above)
    lua_getfield(L, 1, "y");
    lua_pushnil(L);
    if (!lua_istable(L, -2))
        luaL_error(L, "Missing Y-Coordinate table!");
    while (lua_next(L, -2)) {
        double value = lua_tonumber(L, -1);
        yStream.push_back(value);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // Fetch table of pressure values from the Lua stack
    // (same drill as above)
    lua_getfield(L, 1, "pressure");
    lua_pushnil(L);
    if (lua_istable(L, -2)) {
        while (lua_next(L, -2)) {
            double value = lua_tonumber(L, -1);
            pressureStream.push_back(value);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    } else
        g_warning("Missing pressure table. Assuming NO_PRESSURE.");

    // Make sure all vectors are the same length.
    if (xStream.size() != yStream.size()) {
        luaL_error(L, "X and Y vectors are not equal length!");
    }
    if (xStream.size() != pressureStream.size() && pressureStream.size() > 0)
        luaL_error(L, "Pressure vector is not equal length!");

    // Check and make sure there's enough points (need at least 2)
    if (xStream.size() < 2) {
        g_warning("Stroke shorter than four points. Discarding. (Has %ld/2)", xStream.size());
        return 1;
    }
    // Add points to the stroke. Include pressure, if it exists.
    if (pressureStream.size() > 0) {
        for (long unsigned int i = 0; i < xStream.size(); i++) {
            Point myPoint = Point(yStream.at(i), xStream.at(i), pressureStream.at(i));
            stroke->addPoint(myPoint);
        }
    } else {
        for (long unsigned int i = 0; i < xStream.size(); i++) {
            Point myPoint = Point(yStream.at(i), xStream.at(i), Point::NO_PRESSURE);
            stroke->addPoint(myPoint);
        }
    }

    // Finish building the Stroke and apply it to the layer.
    addStrokeHelper(L, stroke, layer);
    return 0;
}

/**
 * Notifies program of any updates to the working document caused
 * by the API.
 *
 * Example: app.refreshPage()
 */
static int applib_refreshPage(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* ctrl = plugin->getControl();
    PageRef const& page = ctrl->getCurrentPage();
    if (page)
        page->firePageChanged();
    else
        g_warning("Called applib_refreshPage, but no page is selected.");
    return 0;
}

/**
 * Change page background of current page
 *
 * Example: app.changeCurrentPageBackground("graph")
 * changes the page background of the current page to graph paper
 */
static int applib_changeCurrentPageBackground(lua_State* L) {
    PageType pt;
    pt.format = PageTypeHandler::getPageTypeFormatForString(luaL_checkstring(L, 1));
    pt.config = luaL_optstring(L, 2, "");

    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* ctrl = plugin->getControl();
    PageBackgroundChangeController* pageBgCtrl = ctrl->getPageBackgroundChangeController();
    pageBgCtrl->changeCurrentPageBackground(pt);

    return 1;
}

/**
 * Change color of a specified tool or of the current tool
 *
 * Example 1: app.changeToolColor({["color"] = 0xff00ff, ["tool"] = "PEN"})
 * changes the color of the pen tool to violet without applying this change to the current selection
 *
 * Example 2: app.changeToolColor({["color"] = 0xff0000, ["selection"] = true })
 * changes the color of the current tool to red and also applies it to the current selection if there is one
 */
static int applib_changeToolColor(lua_State* L) {

    // discard any extra arguments passed in
    lua_settop(L, 1);
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "selection"); /* either true or false, for changing selection color
                                       defaults to false*/
    lua_getfield(L, 1, "tool");      /* "pen", "highlighter", "text"
                                      "select_rect", "select_object", "select_region"
                                      if omitted, current Tool is used */
    lua_getfield(L, 1, "color");     // an RGB hex code defining the color
    // stack now has following:
    //    1 = {["color"] = 0xff00ff, ["tool"] = "PEN", ["selection"] = true}
    //   -3 = true
    //   -2 = "pen"
    //   -1 = 0xff0077

    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* ctrl = plugin->getControl();
    ToolHandler* toolHandler = ctrl->getToolHandler();

    bool selection = false;
    if (lua_isboolean(L, -3)) {
        selection = lua_toboolean(L, -3);
    } else if (!lua_isnil(L, -3)) {
        g_warning(""
                  "selection"
                  " key should be a boolean value (or nil)");
    }

    ToolType toolType = toolHandler->getToolType();
    const char* toolStr = luaL_optstring(L, -2, nullptr);
    if (toolStr != nullptr) {
        toolType = toolTypeFromString(StringUtils::toLowerCase(toolStr));
    }

    if (toolType == TOOL_NONE) {
        g_warning("tool \"%s\" is not valid or no tool has been selected", toolTypeToString(toolType).c_str());
        lua_pop(L, 3);
        return 0;
    }

    uint32_t color = 0x000000;
    if (lua_isinteger(L, -1)) {
        color = as_unsigned(lua_tointeger(L, -1));
        if (color > 0xffffff) {
            g_warning("Color 0x%x is no valid RGB color. ", color);
            return 0;
        }
    } else if (!lua_isnil(L, -1)) {
        g_warning(" "
                  "color"
                  " key should be an RGB hex code in the form 0xRRGGBB (or nil)");
    }

    Tool& tool = toolHandler->getTool(toolType);

    if (tool.hasCapability(TOOL_CAP_COLOR)) {
        tool.setColor(Color(color));
        ctrl->toolColorChanged();
        if (selection)
            ctrl->changeColorOfSelection();
    } else {
        g_warning("tool \"%s\" has no color capability", toolTypeToString(toolType).c_str());
    }

    // Make sure to remove all vars which are put to the stack before!
    lua_pop(L, 3);

    return 1;
}

/*
 * Select Background Pdf Page for Current Page
 * First argument is an integer (page number) and the second argument is a boolean (isRelative)
 * specifying whether the page number is relative to the current pdf page or absolute
 *
 * Example 1: app.changeBackgroundPdfPageNr(1, true)
 * changes the pdf page to the next one (relative mode)
 *
 * Example 2: app.changeBackgroundPdfPageNr(7, false)
 * changes the page background to the 7th pdf page (absolute mode)
 */
static int applib_changeBackgroundPdfPageNr(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);

    size_t nr = luaL_checkinteger(L, 1);
    bool relative = true;
    if (lua_isboolean(L, 2)) {
        relative = lua_toboolean(L, 2);
    }

    Control* control = plugin->getControl();
    Document* doc = control->getDocument();
    PageRef const& page = control->getCurrentPage();

    if (!page) {
        luaL_error(L, "No page!");
    }

    size_t selected = nr - 1;
    if (relative) {
        bool isPdf = page->getBackgroundType().isPdfPage();
        if (isPdf) {
            selected = page->getPdfPageNr() + nr;
        } else {
            luaL_error(L, "Current page has no pdf background, cannot use relative mode!");
        }
    }
    if (selected >= 0 && selected < static_cast<int>(doc->getPdfPageCount())) {
        // no need to set a type, if we set the page number the type is also set
        page->setBackgroundPdfPageNr(selected);

        XojPdfPageSPtr p = doc->getPdfPage(selected);
        page->setSize(p->getWidth(), p->getHeight());
    } else {
        luaL_error(L, "Pdf page number %d does not exist!", selected + 1);
    }

    return 1;
}

/*
 * Returns a table encoding all info on the chosen tool (active, pen, highlighter, eraser or text)
 * in a Lua table of one of the following shapes
 *
 * for pen:
 * {
 *   "size" = string
 *   "color" = integer
 *   "filled" = bool
 *   "fillOpacity" = integer (0 to 255)
 *   "drawingType" = string
 *   "lineStyle" = string
 * }
 *
 * See /src/control/ToolEnums.cpp for possible values of "size".
 *
 * for text:
 * {
 *   "font" = {
 *     name = string.
 *     size = number
 *   }
 *   "color" = integer
 * }
 *
 * for active tool:
 * {
 *   "type" = string
 *   "size" = {
 *     name = string.
 *     value = number
 *   }
 *   "color" = integer
 *   "fillOpacity" = integer (0 to 255)
 *   "drawingType" = string
 *   "lineStyle" = string
 *   "thickness" = number
 * }
 *
 * See /src/control/ToolEnums.cpp for possible values of "type", "size", "drawingType" and "lineStyle".
 *
 * for eraser:
 * {
 *   "type" = string
 *   "size" = string
 * }
 *
 * See /src/control/ToolEnums.cpp for possible values of "type" and "size".
 *
 * for highlighter:
 * {
 *   "size" = string
 *   "color" = integer
 *   "filled" = bool
 *   "fillOpacity" = integer (0 to 255)
 *   "drawingType" = string
 * }
 *
 * See /src/control/ToolEnums.cpp for possible values of "size".
 *
 *
 * Example 1: local penInfo = app.getToolInfo("pen")
 *            local size = penInfo["size"]
 *            local opacity = penInfo["fillOpacity"]
 * *
 * Example 2: local font = app.getToolInfo("text")["font"]
 *            local fontname = font["name"]
 *            local fontsize = font["size"]
 *
 * Example 3: local color = app.getToollInfo("text")["color"]
 *            local red = color >> 16 & 0xff
 *            local green = color >> 8 & 0xff
 *            local blue = color & 0xff
 *
 * Example 4: local activeToolInfo = app.getToolInfo("active")
 *            local thickness = activeToolInfo["thickness"]
 *            local drawingType = activeToolInfo["drawingType"]
 *
 * Example 5: local eraserInfo = app.getToolInfo("eraser")
 *            local type = eraserInfo["type"]
 *            local size = eraserInfo["size"]
 *            local sizeName = size["name"]
 *            local thickness = size["value"]
 *
 * Example 6: local highlighterInfo = app.getToolInfo("highlighter")
 *            local sizeName = highlighterInfo["size"]["name"]
 *            local opacity = highlighterInfo["fillOpacity"]
 */

static int applib_getToolInfo(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();
    ToolHandler* toolHandler = control->getToolHandler();

    const char* mode = luaL_checkstring(L, -1);
    lua_newtable(L);

    if (strcmp(mode, "active") == 0) {
        std::string toolType = toolTypeToString(toolHandler->getToolType());

        std::string toolSize = toolSizeToString(toolHandler->getSize());
        double thickness = toolHandler->getThickness();

        Color color = toolHandler->getColor();
        int fillOpacity = toolHandler->getFill();
        std::string drawingType = drawingTypeToString(toolHandler->getDrawingType());
        std::string lineStyle = StrokeStyle::formatStyle(toolHandler->getLineStyle());


        lua_pushliteral(L, "type");
        lua_pushstring(L, toolType.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "size");
        lua_newtable(L);  // beginning of "size" table

        lua_pushliteral(L, "name");
        lua_pushstring(L, toolSize.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "value");
        lua_pushnumber(L, thickness);
        lua_settable(L, -3);

        lua_settable(L, -3);  // end of "size" table

        lua_pushliteral(L, "color");
        lua_pushinteger(L, int(uint32_t(color)));
        lua_settable(L, -3);

        lua_pushliteral(L, "fillOpacity");
        lua_pushinteger(L, fillOpacity);
        lua_settable(L, -3);

        lua_pushliteral(L, "drawingType");
        lua_pushstring(L, drawingType.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "lineStyle");
        lua_pushstring(L, lineStyle.c_str());
        lua_settable(L, -3);
    } else if (strcmp(mode, "pen") == 0) {
        std::string size = toolSizeToString(toolHandler->getPenSize());
        double thickness = toolHandler->getToolThickness(TOOL_PEN)[toolSizeFromString(size)];

        int fillOpacity = toolHandler->getPenFill();
        bool filled = toolHandler->getPenFillEnabled();

        Tool& tool = toolHandler->getTool(TOOL_PEN);
        Color color = tool.getColor();
        std::string drawingType = drawingTypeToString(tool.getDrawingType());
        std::string lineStyle = StrokeStyle::formatStyle(tool.getLineStyle());

        lua_pushliteral(L, "size");
        lua_newtable(L);  // beginning of "size" table

        lua_pushliteral(L, "name");
        lua_pushstring(L, size.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "value");
        lua_pushnumber(L, thickness);
        lua_settable(L, -3);

        lua_settable(L, -3);  // end of "size" table

        lua_pushliteral(L, "color");
        lua_pushinteger(L, int(uint32_t(color)));
        lua_settable(L, -3);

        lua_pushliteral(L, "drawingType");
        lua_pushstring(L, drawingType.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "lineStyle");
        lua_pushstring(L, lineStyle.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "filled");
        lua_pushboolean(L, filled);
        lua_settable(L, -3);

        lua_pushliteral(L, "fillOpacity");
        lua_pushinteger(L, fillOpacity);
        lua_settable(L, -3);
    } else if (strcmp(mode, "highlighter") == 0) {
        std::string size = toolSizeToString(toolHandler->getHighlighterSize());
        double thickness = toolHandler->getToolThickness(TOOL_HIGHLIGHTER)[toolSizeFromString(size)];

        int fillOpacity = toolHandler->getHighlighterFill();
        bool filled = toolHandler->getHighlighterFillEnabled();

        Tool& tool = toolHandler->getTool(TOOL_HIGHLIGHTER);
        Color color = tool.getColor();
        std::string drawingType = drawingTypeToString(tool.getDrawingType());

        lua_pushliteral(L, "size");
        lua_newtable(L);  // beginning of "size" table

        lua_pushliteral(L, "name");
        lua_pushstring(L, size.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "value");
        lua_pushnumber(L, thickness);
        lua_settable(L, -3);

        lua_settable(L, -3);  // end of "size" table

        lua_pushliteral(L, "color");
        lua_pushinteger(L, int(uint32_t(color)));
        lua_settable(L, -3);

        lua_pushliteral(L, "drawingType");
        lua_pushstring(L, drawingType.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "filled");
        lua_pushboolean(L, filled);
        lua_settable(L, -3);

        lua_pushliteral(L, "fillOpacity");
        lua_pushinteger(L, fillOpacity);
        lua_settable(L, -3);
    } else if (strcmp(mode, "eraser") == 0) {
        std::string type = eraserTypeToString(toolHandler->getEraserType());

        std::string size = toolSizeToString(toolHandler->getEraserSize());
        double thickness = toolHandler->getToolThickness(ToolType::TOOL_ERASER)[toolSizeFromString(size)];

        lua_pushliteral(L, "type");
        lua_pushstring(L, type.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "size");
        lua_newtable(L);  // beginning of "size" table

        lua_pushliteral(L, "name");
        lua_pushstring(L, size.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "value");
        lua_pushnumber(L, thickness);
        lua_settable(L, -3);

        lua_settable(L, -3);  // end of "size" table
    } else if (strcmp(mode, "text") == 0) {
        Settings* settings = control->getSettings();
        XojFont& font = settings->getFont();
        std::string fontname = font.getName();
        double size = font.getSize();

        Tool& tool = toolHandler->getTool(TOOL_TEXT);
        Color color = tool.getColor();

        lua_newtable(L);

        lua_pushliteral(L, "font");
        lua_newtable(L);

        lua_pushliteral(L, "name");
        lua_pushstring(L, fontname.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "size");
        lua_pushnumber(L, size);
        lua_settable(L, -3);

        lua_settable(L, -3);

        lua_pushliteral(L, "color");
        lua_pushinteger(L, int(uint32_t(color)));
        lua_settable(L, -3);
    }
    return 1;
}

/*
 * Returns a table encoding the document structure in a Lua table of the shape
 * {
 *   "pages" = {
 *     {
 *       "pageWidth" = number,
 *       "pageHeight" = number,
 *       "isAnnoated" = bool,
 *       "pageTypeFormat" = string,
 *       "pdfBackgroundPageNo" = integer (0, if there is no pdf background page),
 *       "layers" = {
 *         [0] = {
 *           "isVisible" = bool
 *         },
 *         [1] = {
 *           "isVisible" = bool,
 *           "isAnnotated" = bool
 *         },
 *         ...
 *       },
 *       "currentLayer" = integer
 *     },
 *     ...
 *   }
 *   "currentPage" = integer,
 *   "pdfBackgroundFilename" = string (empty if there is none)
 * }
 *
 * Example: local docStructure = app.getDocumentStructure()
 */
static int applib_getDocumentStructure(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();
    Document* doc = control->getDocument();
    LayerController* lc = control->getLayerController();

    lua_newtable(L);

    lua_pushliteral(L, "pages");
    lua_newtable(L);  // beginning of pages table

    // add pages
    for (size_t p = 1; p <= doc->getPageCount(); ++p) {
        auto page = doc->getPage(p - 1);
        lua_pushinteger(L, p);
        lua_newtable(L);  // beginning of table for page p

        lua_pushliteral(L, "pageWidth");
        lua_pushnumber(L, page->getWidth());
        lua_settable(L, -3);

        lua_pushliteral(L, "pageHeight");
        lua_pushnumber(L, page->getHeight());
        lua_settable(L, -3);

        lua_pushliteral(L, "isAnnotated");
        lua_pushboolean(L, page->isAnnotated());
        lua_settable(L, -3);

        lua_pushliteral(L, "pageTypeFormat");
        PageType pt = page->getBackgroundType();
        std::string pageTypeFormat = PageTypeHandler::getStringForPageTypeFormat(pt.format);
        lua_pushstring(L, pageTypeFormat.c_str());
        lua_settable(L, -3);

        lua_pushliteral(L, "pdfBackgroundPageNo");
        lua_pushinteger(L, page->getPdfPageNr() + 1);
        lua_settable(L, -3);

        lua_pushstring(L, "layers");
        lua_newtable(L);  // beginning of layers table

        // add background layer
        lua_pushinteger(L, 0);
        lua_newtable(L);  // beginning of table for background layer

        lua_pushliteral(L, "isVisible");
        lua_pushboolean(L, page->isLayerVisible(0));
        lua_settable(L, -3);

        lua_pushliteral(L, "name");
        lua_pushstring(L, page->getBackgroundName().c_str());
        lua_settable(L, -3);

        lua_settable(L, -3);  // end of table for background layer

        // add (non-background) layers
        int currLayer = 0;

        for (auto l: *page->getLayers()) {
            lua_pushinteger(L, ++currLayer);
            lua_newtable(L);  // beginning of table for layer l

            lua_pushliteral(L, "name");
            lua_pushstring(L, lc->getLayerNameById(currLayer).c_str());
            lua_settable(L, -3);

            lua_pushliteral(L, "isVisible");
            lua_pushboolean(L, l->isVisible());
            lua_settable(L, -3);

            lua_pushliteral(L, "isAnnotated");
            lua_pushboolean(L, l->isAnnotated());
            lua_settable(L, -3);

            lua_settable(L, -3);  // end of table for layer l
        }
        lua_settable(L, -3);  // end of layers table

        lua_pushliteral(L, "currentLayer");
        lua_pushinteger(L, page->getSelectedLayerId());
        lua_settable(L, -3);

        lua_settable(L, -3);  // end of table for page p
    }
    lua_settable(L, -3);  // end of pages table

    lua_pushliteral(L, "currentPage");
    lua_pushinteger(L, lc->getCurrentPageId() + 1);
    lua_settable(L, -3);

    lua_pushliteral(L, "pdfBackgroundFilename");
    lua_pushstring(L, doc->getPdfFilepath().string().c_str());
    lua_settable(L, -3);

    return 1;
}

/**
 * Scrolls to the page specified relatively or absolutely (by default)
 * The page number is clamped to the range between the first and last page
 *
 * Example 1: app.scrollToPage(1, true)
 * scrolls to the next page (relative mode)
 *
 * Example 2: app.scrollToPage(10)
 * scrolls to page 10 (absolute mode)
 **/
static int applib_scrollToPage(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();

    int val = luaL_checkinteger(L, 1);
    bool relative = false;
    if (lua_isboolean(L, 2)) {
        relative = lua_toboolean(L, 2);
    }
    int page = (relative) ? control->getCurrentPageNo() + val : val - 1;

    const int first = 0;
    const int last = static_cast<int>(control->getDocument()->getPageCount()) - 1;
    control->getScrollHandler()->scrollToPage(std::clamp(page, first, last));

    return 1;
}

/**
 * Scrolls to the position on the selected page specified relatively (by default) or absolutely
 *
 * Example 1: app.scrollToPos(20,10)
 * scrolls 20pt right and 10pt down (relative mode)
 *
 * Example 2: app.scrollToPos(200, 50, false)
 * scrolls to page position 200pt right and 50pt down from the left page corner  (absolute mode)
 **/
static int applib_scrollToPos(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();
    Layout* layout = control->getWindow()->getLayout();

    double dx = luaL_checknumber(L, 1);
    double dy = luaL_checknumber(L, 2);
    bool relative = true;
    if (lua_isboolean(L, 3)) {
        relative = lua_toboolean(L, 3);
    }

    if (relative) {
        layout->scrollRelative(dx, dy);
    } else {
        layout->scrollAbs(dx, dy);
    }

    return 1;
}

/**
 * Sets the current page as indicated (without scrolling)
 * The page number passed is clamped to the range between first page and last page
 *
 * Example: app.setCurrentPage(1)
 * makes the first page the new current page
 **/
static int applib_setCurrentPage(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();
    size_t pageId = luaL_checkinteger(L, 1);
    const size_t first = 1;
    const size_t last = control->getDocument()->getPageCount();
    control->firePageSelected(std::clamp(pageId, first, last) - 1);

    return 1;
}

/**
 * Sets the width and height of the current page in pt = 1/72 inch either relatively or absolutely (by default)
 *
 * Example 1: app.setPageSize(595.275591, 841.889764)
 * makes the current page have standard (A4 paper) width and height (absolute mode)
 *
 * Example 2: app.setPageSize(0, 14.17*6, true)
 * adds 14.17*6 pt = 3cm to the height of the page (relative mode)
 **/
static int applib_setPageSize(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();
    Document* doc = control->getDocument();
    PageRef const& page = control->getCurrentPage();

    if (!page) {
        luaL_error(L, "No page!");
    }

    double width = luaL_checknumber(L, 1);
    double height = luaL_checknumber(L, 2);

    bool relative = false;
    if (lua_isboolean(L, 3)) {
        relative = lua_toboolean(L, 3);
    }

    if (relative) {
        width += page->getWidth();
        height += page->getHeight();
    }

    if (width > 0 && height > 0) {
        doc->lock();
        Document::setPageSize(page, width, height);
        doc->unlock();
    }

    size_t pageNo = doc->indexOf(page);
    if (pageNo != npos && pageNo < doc->getPageCount()) {
        control->firePageSizeChanged(pageNo);
    }

    return 1;
}

/**
 * Sets the current layer of the current page as indicated and updates visibility if specified (by default it does not)
 * Displays an error message, if the selected layer does not exist
 *
 * Example 1: app.setCurrentLayer(2, true)
 * makes the second (non-background) layer the current layer and makes layers 1, 2 and the background layer visible, the
 *others hidden
 *
 * Example 2: app.setCurrentLayer(2, false)
 * makes the second (non-background) layer the current layer and does not change visibility
 **/
static int applib_setCurrentLayer(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();
    PageRef const& page = control->getCurrentPage();

    if (!page) {
        luaL_error(L, "No page!");
    }

    size_t layerCount = page->getLayerCount();
    size_t layerId = luaL_checkinteger(L, 1);

    if (layerId < 0 || layerId > layerCount) {
        luaL_error(L, "No layer with layer ID %d", layerId);
    }

    bool update = false;
    if (lua_isboolean(L, 2)) {
        update = lua_toboolean(L, 2);
    }

    control->getLayerController()->switchToLay(layerId, update);

    return 1;
}

/*
 * Sets the visibility of the current layer
 *
 * Example: app.setLayerVisibility(true)
 * makes the current layer visible
 */
static int applib_setLayerVisibility(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();

    bool enabled = true;
    if (lua_isboolean(L, 1)) {
        enabled = lua_toboolean(L, 1);
    }

    int layerId = control->getCurrentPage()->getSelectedLayerId();
    control->getLayerController()->setLayerVisible(layerId, enabled);
    return 1;
}

/**
 * Sets currently selected layer's name.
 *
 * Example: app.setCurrentLayerName("Custom name 1")
 * Changes current layer name to "Custom name 1"
 **/
static int applib_setCurrentLayerName(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();

    if (lua_isstring(L, 1)) {
        auto name = lua_tostring(L, 1);
        control->getLayerController()->setCurrentLayerName(name);
    }

    return 1;
}

/**
 * Sets background name.
 *
 * Example: app.setBackgroundName("Custom name 1")
 * Changes background name to "Custom name 1"
 **/
static int applib_setBackgroundName(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();
    PageRef const& page = control->getCurrentPage();

    if (!page) {
        luaL_error(L, "No page!");
    }

    if (lua_isstring(L, 1)) {
        auto name = lua_tostring(L, 1);
        page->setBackgroundName(name);
    }

    return 1;
}


/**
 * Scales all text elements of the current layer by the given scale factor.
 * This means the font sizes get scaled, wheras the position of the left upper corner
 * of the bounding box remains unchanged
 *
 * Example: app.scaleTextElements(2.3)
 * scales all text elements on the current layer with factor 2.3
 **/
static int applib_scaleTextElements(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();

    double f = luaL_checknumber(L, 1);

    control->clearSelectionEndText();

    const std::vector<Element*>& elements = control->getCurrentPage()->getSelectedLayer()->getElements();

    for (Element* e: elements) {
        if (e->getType() == ELEMENT_TEXT) {
            Text* t = static_cast<Text*>(e);
            t->scale(t->getX(), t->getY(), f, f, 0.0, false);
        }
    }

    return 1;
}


/**
 * Gets the display DPI.
 * Example: app.getDisplayDpi()
 **/
static int applib_getDisplayDpi(lua_State* L) {
    Plugin* plugin = Plugin::getPluginFromLua(L);
    Control* control = plugin->getControl();
    int dpi = control->getSettings()->getDisplayDpi();
    lua_pushinteger(L, dpi);

    return 1;
}


/*
 * The full Lua Plugin API.
 * See above for example usage of each function.
 */
static const luaL_Reg applib[] = {{"msgbox", applib_msgbox},
                                  {"glib_rename", applib_glib_rename},
                                  {"saveAs", applib_saveAs},
                                  {"registerUi", applib_registerUi},
                                  {"uiAction", applib_uiAction},
                                  {"uiActionSelected", applib_uiActionSelected},
                                  {"sidebarAction", applib_sidebarAction},
                                  {"layerAction", applib_layerAction},
                                  {"changeToolColor", applib_changeToolColor},
                                  {"changeCurrentPageBackground", applib_changeCurrentPageBackground},
                                  {"changeBackgroundPdfPageNr", applib_changeBackgroundPdfPageNr},
                                  {"getToolInfo", applib_getToolInfo},
                                  {"getDocumentStructure", applib_getDocumentStructure},
                                  {"scrollToPage", applib_scrollToPage},
                                  {"scrollToPos", applib_scrollToPos},
                                  {"setCurrentPage", applib_setCurrentPage},
                                  {"setPageSize", applib_setPageSize},
                                  {"setCurrentLayer", applib_setCurrentLayer},
                                  {"setLayerVisibility", applib_setLayerVisibility},
                                  {"setCurrentLayerName", applib_setCurrentLayerName},
                                  {"setBackgroundName", applib_setBackgroundName},
                                  {"scaleTextElements", applib_scaleTextElements},
                                  {"getDisplayDpi", applib_getDisplayDpi},
                                  {"addStroke", applib_addStroke},
                                  {"addSpline", applib_addSpline},
                                  {"getFilePath", applib_getFilePath},
                                  {"refreshPage", applib_refreshPage},
                                  // Placeholder
                                  //	{"MSG_BT_OK", nullptr},

                                  {nullptr, nullptr}};

/**
 * Open application Library
 */
LUAMOD_API int luaopen_app(lua_State* L) {
    luaL_newlib(L, applib);
    //	lua_pushnumber(L, MSG_BT_OK);
    //	lua_setfield(L, -2, "MSG_BT_OK");
    return 1;
}
