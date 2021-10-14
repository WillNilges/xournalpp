-- This is an example Xournal++ Plugin - copy this to get started

-- var_dump = require "var_dump"

-- Register all Toolbar actions and intialize all UI stuff
function initUi()
  print("Hello from ImageTranscription: Plugin initUi called\n");

  ref = app.registerUi({["menu"] = "Transcribe Image", ["callback"] = "drawStroke", ["accelerator"] = "<Control><Shift>t"});
  -- print("Menu reference:");
  -- var_dump(ref);

  print("ImageTranscription registered\n");
end

-- Callback if the menu item is executed
-- TODO: Place stroke on page
function drawStroke()
  -- result = app.msgbox("Test123", {[1] = "Yes", [2] = "No"});
  app.drawStroke();
  print("result is we're done")
end
