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
  inkpath = require 'inkpath'
  local inspect = require 'inspect'
  -- transcribed = 
  -- How to access stack values!?
  -- How to return values!?
  -- strokes = inkpath.transcribe_image("/home/wilnil/inkpath/data/20211019_181644.jpg")
  strokes = inkpath.transcribe_image("/home/wilnil/inkpath/data/cropped/20180130_080919.jpg")
  print("Here are our strokes.")
  -- print(inspect(strokes))
  -- stroke_count = 

  single_stroke = {}
  for key, value in pairs(strokes) do
      -- print(key, " -- ", value)
      if value[1] == -1.0 and value[2] == -1.0 then
      --  print("Encountered -1, -1 pair.")
        app.drawStroke(single_stroke)
        single_stroke = {}
      -- print(type(value[1]))
      -- print(type(value[2]))
      else
        table.insert(single_stroke, value[2]) -- Y coord
        table.insert(single_stroke, value[1]) -- X coord
      end
      -- for key2, value2 in pairs(value) do
      --     -- print(key2, " -- ", value2)
      --     table.insert(single_stroke, value2)
      -- end
  end
  -- app.drawStroke(strokes); -- Pass a stroke to the C++ function
  print("result is we're done")
end
