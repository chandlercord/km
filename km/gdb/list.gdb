#
# print BSD list content (see 'man queue')
# Can be used for printing mmaps free/busy queur
#

define print_list
if $argc == 0
      help print_list
      return
   else
      print $arg0
      set $head = $arg0
   end

   set $item = $head->lh_first
   while $item != 0
      printf "start= 0x%lx size = %d MiB\n", $item->start, $item->size/1024/1024
      set $item=$item->link->le_next
   end
end

document print_list
   Prints LIST info. Assumes LIST entry name is 'link'
     Syntax: print_list &LIST
   Examples:
     print_list &mmaps.free
end


define print_tailq
if $argc == 0
      help print_tailq
      return
   else
      print $arg0
      set $head = $arg0
   end

   set $item = $head->tqh_first
   while $item != 0
      printf "start= 0x%lx size = %d MiB\n", $item->start, $item->size/1024/1024
      set $item=$item->link->tqe_next
   end
end

document print_tailq
   Prints TAILQ info. Assumes TAILQ entry name is 'link'
     Syntax: print_tailq &LIST
   Examples:
     print_tailq &mmaps.free
end
