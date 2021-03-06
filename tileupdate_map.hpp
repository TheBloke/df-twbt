static void resolve_color(int fg, int bg, int bold, struct texture_fullid &ret)
{
    if (fg >= 100)
    {
        fg = (fg-100) % df::global::world->raws.language.colors.size();
        df::descriptor_color *fgdc = df::global::world->raws.language.colors[fg];

        ret.r = fgdc->red;
        ret.g = fgdc->green;
        ret.b = fgdc->blue;
    }
    else
    {
        fg = (fg + bold * 8) % 16;

        ret.r = enabler->ccolor[fg][0];
        ret.g = enabler->ccolor[fg][1];
        ret.b = enabler->ccolor[fg][2];
    }

    if (bg >= 100)
    {
        bg = (bg-100) % df::global::world->raws.language.colors.size();
        df::descriptor_color *bgdc = df::global::world->raws.language.colors[bg];

        ret.br = bgdc->red;
        ret.bg = bgdc->green;
        ret.bb = bgdc->blue;
    }
    else
    {
        bg = bg % 16;

        ret.br = enabler->ccolor[bg][0];
        ret.bg = enabler->ccolor[bg][1];
        ret.bb = enabler->ccolor[bg][2];
    }    
}

static void screen_to_texid_map(renderer_cool *r, int tile, struct texture_fullid &ret)
{
    const unsigned char *s = gscreen + tile*4;

    int fg   = s[1];
    int bg   = s[2];
    int bold = s[3] & 0x0f;

    const long texpos = gscreentexpos[tile];

    if (!texpos)
    {
        ret.texpos = map_texpos[s[0]];

        resolve_color(fg, bg, bold, ret);
        return;
    }        

    ret.texpos = texpos;

    if (gscreentexpos_grayscale[tile])
    {
        const unsigned char cf = gscreentexpos_cf[tile];
        const unsigned char cbr = gscreentexpos_cbr[tile];

        resolve_color(cf, cbr, 0, ret);
    }
    else if (gscreentexpos_addcolor[tile])
        resolve_color(fg, bg, bold, ret);
    else
    {
        ret.r = ret.g = ret.b = 1;
        ret.br = ret.bg = ret.bb = 0;
    }
}

static void apply_override (texture_fullid &ret, override &o)
{
    if (o.large_texpos)
        ret.texpos = enabler->fullscreen ? o.large_texpos : o.small_texpos;
    if (o.bg != -1)
    {
        ret.br = enabler->ccolor[o.bg][0];
        ret.bg = enabler->ccolor[o.bg][1];
        ret.bb = enabler->ccolor[o.bg][2];        
    }
    if (o.fg != -1)
    {
        ret.r = enabler->ccolor[o.fg][0];
        ret.g = enabler->ccolor[o.fg][1];
        ret.b = enabler->ccolor[o.fg][2];        
    }
}

static void write_tile_arrays_map(renderer_cool *r, int x, int y, GLfloat *fg, GLfloat *bg, GLfloat *tex)
{
    struct texture_fullid ret;
    const int tile = x * r->gdimy + y;        
    screen_to_texid_map(r, tile, ret);
    
    if (has_overrides && world->map.block_index)
    {
        const unsigned char *s = gscreen + tile*4;
        int s0 = s[0];

        if (overrides[s0])
        {
            int xx = gwindow_x + x;
            int yy = gwindow_y + y;

            if (xx >= 0 && yy >= 0 && xx < world->map.x_count && yy < world->map.y_count)
            {

                if (s0 == 88 && df::global::cursor->x == xx && df::global::cursor->y == yy)
                {
                    long texpos = enabler->fullscreen ? cursor_large_texpos : cursor_small_texpos;
                    if (texpos)
                        ret.texpos = texpos;
                }
                else
                {
                    int zz = gwindow_z - ((s[3]&0xf0)>>4);

                    tile_overrides *to = overrides[s0];

                    // Items
                    for (auto it = to->item_overrides.begin(); it != to->item_overrides.end(); it++)
                    {
                        override_group &og = *it;

                        auto ilist = world->items.other[og.other_id];
                        for (auto it2 = ilist.begin(); it2 != ilist.end(); it2++)
                        {
                            df::item *item = *it2;
                            if (!(zz == item->pos.z && xx == item->pos.x && yy == item->pos.y))
                                continue;
                            if (item->flags.whole & bad_item_flags.whole)
                                continue;

                            for (auto it3 = og.overrides.begin(); it3 != og.overrides.end(); it3++)
                            {
                                override &o = *it3;

                                if (o.type != -1 && item->getType() != o.type)
                                    continue;
                                if (o.subtype != -1 && item->getSubtype() != o.subtype)
                                    continue;

                                apply_override(ret, o);
                                goto matched;
                            }
                        }
                    }

                    // Buildings
                    for (auto it = to->building_overrides.begin(); it != to->building_overrides.end(); it++)
                    {
                        override_group &og = *it;

                        auto ilist = world->buildings.other[og.other_id];
                        for (auto it2 = ilist.begin(); it2 != ilist.end(); it2++)
                        {
                            df::building *bld = *it2;
                            if (zz != bld->z || xx < bld->x1 || xx > bld->x2 || yy < bld->y1 || yy > bld->y2)
                                continue;

                            for (auto it3 = og.overrides.begin(); it3 != og.overrides.end(); it3++)
                            {
                                override &o = *it3;

                                if (o.type != -1 && bld->getType() != o.type)
                                    continue;
                                
                                if (o.subtype != -1)
                                {
                                    int subtype = (og.other_id == buildings_other_id::WORKSHOP_CUSTOM || og.other_id == buildings_other_id::FURNACE_CUSTOM) ?
                                        bld->getCustomType() : bld->getSubtype();

                                    if (subtype != o.subtype)
                                        continue;
                                }

                                apply_override(ret, o);
                                goto matched;
                            }
                        }
                    }

                    // Tile types
                    df::map_block *block = world->map.block_index[xx>>4][yy>>4][zz];
                    if (block)
                    {
                        int tiletype = block->tiletype[xx&15][yy&15];

                        for (auto it3 = to->tiletype_overrides.begin(); it3 != to->tiletype_overrides.end(); it3++)
                        {
                            override &o = *it3;

                            if (tiletype == o.type)
                            {
                                apply_override(ret, o);
                                goto matched;
                            }
                        }
                    }
                }
            }
        }
    }
    matched:;

    // Set colour
    for (int i = 0; i < 2; i++) {
        fg += 8;
        *(fg++) = ret.r;
        *(fg++) = ret.g;
        *(fg++) = ret.b;
        *(fg++) = 1;
        
        bg += 8;
        *(bg++) = ret.br;
        *(bg++) = ret.bg;
        *(bg++) = ret.bb;
        *(bg++) = 1;
    }    
    
    // Set texture coordinates
    gl_texpos *txt = (gl_texpos*) enabler->textures.gl_texpos;
    *(tex++) = txt[ret.texpos].left;   // Upper left
    *(tex++) = txt[ret.texpos].bottom;
    *(tex++) = txt[ret.texpos].right;  // Upper right
    *(tex++) = txt[ret.texpos].bottom;
    *(tex++) = txt[ret.texpos].left;   // Lower left
    *(tex++) = txt[ret.texpos].top;
    
    *(tex++) = txt[ret.texpos].left;   // Lower left
    *(tex++) = txt[ret.texpos].top;
    *(tex++) = txt[ret.texpos].right;  // Upper right
    *(tex++) = txt[ret.texpos].bottom;
    *(tex++) = txt[ret.texpos].right;  // Lower right
    *(tex++) = txt[ret.texpos].top;
}