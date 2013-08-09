rfx
===

RF online proxy/filter

Usage:

filter:

/loot all-@bad    -- shows all items except items listed in @bad
/loot @good       -- shows only items listed in @good
/loot save good   -- save current filter mask to file
/loot +0x1234     -- show item
/loot -0x1234     -- hide item

auto loot:

/iq on            -- enable auto looting
/iq off           -- disables auto looting

/iq seq/inv/rand  -- sets order for auto looting (in loot drop order)
                      - sec - direct order (from first dropped item to last dropped item)
                      - inv - reverse order (from last dropped item to first dropped item)
                      - rand - random order
