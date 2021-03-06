#include "Renderer_TD.h"
#include "GameBasics_TD.h"

inline void GetWindowDimensions(HWND Window, v2i *Dim);
inline renderer
InitializeRenderer(game_state *GameState, HWND WindowHandle)
{
    renderer Result = {};
    
    GetWindowDimensions(WindowHandle, &Result.Window.FixedDim.Dim);
    Result.Window.FixedAspect = (r32)Result.Window.FixedDim.Width/(r32)Result.Window.FixedDim.Height;
    Result.Window.CurrentDim = Result.Window.FixedDim;
    
    Result.Window.CurrentAspect        = (r32)Result.Window.CurrentDim.Width/(r32)Result.Window.CurrentDim.Height;
    Result.Window.CurrentReverseAspect = (r32)Result.Window.CurrentDim.Height/(r32)Result.Window.CurrentDim.Width;
    Result.Window.WindowHandle = WindowHandle;
    
    Result.BackgroundColor = {0.5f, 0.5f, 0.5f};//{3.0f/255, 17.0f/255, 3.0f/255, 1};
    Result.DefaultEntryColor = {1, 1, 1};
    
    Result.TransformList  = CreateScreenTransformList(&GameState->Bucket.Fixed, 1024);
    
    Result.RenderEntryList.Entries = PushArrayOnBucket(&GameState->Bucket.Fixed, START_RENDER_ENTRIES, render_entry);
    Result.RenderEntryList.IDs     = PushArrayOnBucket(&GameState->Bucket.Fixed, START_RENDER_ENTRIES, entry_id);
    Result.RenderEntryList.OpenSlots = PushArrayOnBucket(&GameState->Bucket.Fixed, START_RENDER_ENTRIES, b32);
    Result.RenderEntryList.MaxCount = START_RENDER_ENTRIES;
    
    return Result;
}

inline rect_2D
Rect(v2 Min, v2 Max)
{
    rect_2D Result = {Min, Max};
    return Result;
}

inline v2
GetCenter(rect_2D Rect)
{
    v2 Result = Rect.Min + (Rect.Max-Rect.Min)*0.5f;
    return Result;
}

inline v2
GetExtends(rect_2D Rect)
{
    v2 Result = Rect.Max-Rect.Min;
    return Result;
}

inline rect_pe_2D
RectToRectPE(rect Rect)
{
    rect_pe_2D Result = {};
    
    Result.Extends = (Rect.Max - Rect.Min)*0.5f;
    Result.Pos = Rect.Min + Result.Extends;
    
    return Result;
}


inline void
MonoBitmapToRGBA(u8 *MonoBitmap, loaded_bitmap *Bitmap)
{
    u8 *Source = MonoBitmap;
    u8 *DestRow = (u8 *)Bitmap->Pixels;
    for(u32 Y = 0; Y < Bitmap->Height; Y++)
    {
        u32 *Dest = (u32 *)DestRow;
        for(u32 X = 0; X < Bitmap->Width; X++)
        {
            u8 Alpha = *Source++;
            *Dest++ = ((Alpha << 24) |
                       (Alpha << 16) | 
                       (Alpha << 8)  |
                       (Alpha << 0));
        }
        DestRow += Bitmap->Pitch;
    }
    Bitmap->WasLoaded = true;
}

internal render_text_atlas *
InitSTBBakeFont(game_state *GameState, r32 FontHeightInPixel)
{
    render_text_atlas *Result = PushStructOnBucket(&GameState->Bucket.Fixed, render_text_atlas);
    
    read_file_result FontData = {};
    if(ReadEntireFile(&GameState->Bucket.Transient, &FontData, (u8 *)"..\\data\\Fonts\\carmini.ttf"))
    {
        u32 BitmapSize = 1200;
        loaded_bitmap Bitmap = {0, BitmapSize, BitmapSize, 0, colorFormat_RGBA, BitmapSize*4};
        Bitmap.Pixels          = PushArrayOnBucket(&GameState->Bucket.Fixed, Bitmap.Width*Bitmap.Height, u32);
        u8 *AlphaMap           = PushArrayOnBucket(&GameState->Bucket.Transient, Bitmap.Width*Bitmap.Height, u8);
        
        int BakeRet = stbtt_BakeFontBitmap(FontData.Data, 0, FontHeightInPixel, 
                                           AlphaMap, Bitmap.Width, Bitmap.Height, 
                                           32, ATLAS_LETTER_COUNT, Result->CharData);
        
        MonoBitmapToRGBA(AlphaMap, &Bitmap);
        PopFromTransientBucket(&GameState->Bucket.Transient, AlphaMap);
        
        Result->Bitmap = Bitmap;
        Result->GLID  = CreateGLTexture(Bitmap);
        
        FreeFileMemory(&GameState->Bucket.Transient, FontData.Data);
    }
    return Result;
}

inline void
ApplyTransform(render_entry *Entry, v3 *Result)
{
    transform_2D *T = &Entry->Transform;
    v2 Dim    = Entry->Vertice[2].xy - Entry->Vertice[0].xy;
    v2 Center = Entry->Vertice[0].xy + Dim*0.5f;
    
    v2 ParentTranslation = {};
    if(Entry->Parent)
    {
        ParentTranslation = GetPosition(Entry->Parent);
        switch(Entry->FixedTo)
        {
            case fixedTo_None: break;
            case fixedTo_BottomLeft:    // #Through
            case fixedTo_BottomRight:   // #Through
            case fixedTo_BottomCenter:  // #Through
            case fixedTo_TopLeft:       // #Through
            case fixedTo_TopRight:      // #Through
            case fixedTo_TopCenter:     // #Through
            case fixedTo_LeftCenter:    // #Through 
            case fixedTo_RightCenter:   // #Through
            case fixedTo_Center: ParentTranslation = {}; break;
            case fixedTo_FixXToGiven_YBottom: // #Through
            case fixedTo_FixXToGiven_YCenter: // #Through
            case fixedTo_FixXToGiven_YTop: 
            {
                T->Translation.x = 0;
                ParentTranslation.y = 0; 
            } break;
            case fixedTo_FixYToGiven_XLeft:   // #Through
            case fixedTo_FixYToGiven_XCenter: // #Through
            case fixedTo_FixYToGiven_XRight:  
            {
                T->Translation.y = 0;
                ParentTranslation.x = 0;
            } break;
            case fixedTo_FixXYToGiven: break;
        }
    }
    
    For(4)
    {
        Result[It] = Entry->Vertice[It];
        
        // Scaling
        Result[It].xy -= Center;
        Result[It].xy  = HadamardProduct(T->Scale, Result[It].xy);
        Result[It].xy += Center;
        // Translation
        Result[It].xy += T->Translation;
        // Rotation
        Result[It].xy  = Rotate(Result[It].xy, T->RotationP, T->Angle);
        
        Result[It].xy += ParentTranslation;
    }
}

inline button *
NewButton(renderer *Renderer, rect Rect, r32 Depth, b32 IsToggle,
          string_c *BtnPath, v3 *BaseColor, v3 *DownColor, v3 *HoverColor, string_c *IconPath, v3 *IconColor,
          entry_id *Parent, string_c *ToggleIconPath)
{
    Assert(Renderer->ButtonGroup.Count < MAX_BUTTONS);
    button *Result = Renderer->ButtonGroup.Buttons + Renderer->ButtonGroup.Count++;
    Result->Entry = CreateRenderBitmap(Renderer, Rect, Depth, Parent, BtnPath);
    Result->Entry->ID->Color = BaseColor;
    Result->BaseColor = BaseColor;
    Result->DownColor = DownColor;
    Result->HoverColor = HoverColor;
    Result->IsToggle = IsToggle;
    
    Result->Icon = CreateRenderBitmap(Renderer, Rect, Depth-0.0000001f, Result->Entry, IconPath);
    Result->Icon->ID->Color = IconColor;
    
    if(IsToggle)
    {
        Assert(ToggleIconPath);
        Result->ToggleIcon = CreateRenderBitmap(Renderer, Rect, Depth-0.0000001f, Result->Entry, ToggleIconPath);
        Result->ToggleIcon->ID->Render = false;
        Result->ToggleIcon->ID->Color = IconColor;
    }
    
    return Result;
}


inline button *
NewButton(renderer *Renderer, rect Rect, r32 Depth, b32 IsToggle,
          u32 ButtonBitmapID, v3 *BaseColor, v3 *DownColor, v3 *HoverColor, 
          u32 IconBitmapID, v3 *IconColor, entry_id *Parent, i32 ToggleIconBitmapID)
{
    Assert(Renderer->ButtonGroup.Count < MAX_BUTTONS);
    button *Result = Renderer->ButtonGroup.Buttons + Renderer->ButtonGroup.Count++;
    Result->Entry = CreateRenderBitmap(Renderer, Rect, Depth, Parent, ButtonBitmapID);
    Result->Entry->ID->Color = BaseColor;
    Result->BaseColor = BaseColor;
    Result->DownColor = DownColor;
    Result->HoverColor = HoverColor;
    Result->IsToggle = IsToggle;
    
    
    Result->Icon = CreateRenderBitmap(Renderer, Rect, Depth-0.0000001f, Result->Entry, IconBitmapID);
    Result->Icon->ID->Color = IconColor;
    
    if(IsToggle)
    {
        Assert(ToggleIconBitmapID >= 0);
        Result->ToggleIcon = CreateRenderBitmap(Renderer, Rect, Depth-0.0000001f, Result->Entry, ToggleIconBitmapID);
        Result->ToggleIcon->ID->Render = false;
        Result->ToggleIcon->ID->Color = IconColor;
    }
    
    return Result;
}

internal void
ButtonTestMouseInteraction(renderer *Renderer, input_info *Input, button *Button)
{
    b32 MouseInsideButton = false;
    MouseInsideButton = IsInRect(Button->Entry, Input->MouseP);
    
    if(Button->IsToggle)
    {
        if(MouseInsideButton)
        {
            if(Input->KeyChange[KEY_LMB] == KeyDown) // Btn starts being pressed down
            {
                Button->ClickedInBtn = true;
                if(Button->State != buttonState_Pressed)
                {
                    Button->State = buttonState_Pressed;
                    Button->Entry->ID->Color = Button->DownColor;
                    Button->Icon->ID->Render = false;
                    Button->ToggleIcon->ID->Render = true;
                }
                else
                {
                    Button->State = buttonState_Hover;
                    Button->Entry->ID->Color = Button->HoverColor;
                    Button->Icon->ID->Render = true;
                    Button->ToggleIcon->ID->Render = false;
                    if(Button->OnHoverEnter.Func) Button->OnHoverEnter.Func(Button->OnHoverEnter.Data);
                }
            }
            else if(Button->State == buttonState_Unpressed) // Btn starts being hovered
            {
                Button->State = buttonState_Hover;
                Button->Entry->ID->Color = Button->HoverColor;
                if(Button->OnHoverEnter.Func) Button->OnHoverEnter.Func(Button->OnHoverEnter.Data);
            }
            else if(Input->KeyChange[KEY_LMB] == KeyUp && 
                    Button->ClickedInBtn) // Btn press ended
            {
                Button->ClickedInBtn = false;
                if(!Button->Icon->ID->Render && Button->OnPressed.Func) Button->OnPressed.Func(Button->OnPressed.Data);
                else if(Button->OnPressedToggleOff.Func) Button->OnPressedToggleOff.Func(Button->OnPressed.Data);
            }
        }
        else
        {
            if(Button->State == buttonState_Hover) // Btn being exited
            {
                Button->State = buttonState_Unpressed;
                Button->Entry->ID->Color = Button->BaseColor;
                if(Button->OnHoverExit.Func) Button->OnHoverExit.Func(Button->OnHoverExit.Data);
            }
            else if(Input->Pressed[KEY_LMB] && Button->State == buttonState_Pressed &&
                    Button->ClickedInBtn)
            {
                Button->State = buttonState_Unpressed;
                Button->Icon->ID->Render = true;
                Button->ToggleIcon->ID->Render = false;
            }
            if(Input->KeyChange[KEY_LMB] == KeyUp && Button->ClickedInBtn) Button->ClickedInBtn = false;
        }
    }
    else
    {
        if(MouseInsideButton)
        {
            if(Input->KeyChange[KEY_LMB] == KeyDown) // Btn starts being pressed down
            {
                Button->State = buttonState_Pressed;
                Button->Entry->ID->Color = Button->DownColor;
            }
            else if(Button->State == buttonState_Unpressed) // Btn starts being hovered
            {
                Button->State = buttonState_Hover;
                Button->Entry->ID->Color = Button->HoverColor;
                if(Button->OnHoverEnter.Func) Button->OnHoverEnter.Func(Button->OnHoverEnter.Data);
            }
            else if(Input->KeyChange[KEY_LMB] == KeyUp && 
                    Button->State == buttonState_Pressed) // Btn press ended
            {
                Button->State = buttonState_Hover;
                Button->Entry->ID->Color = Button->HoverColor;
                if(Button->OnPressed.Func) Button->OnPressed.Func(Button->OnPressed.Data);
            }
        }
        else
        {
            if(Button->State == buttonState_Hover ||
               Button->State == buttonState_Pressed) // Btn being exited
            {
                Button->State = buttonState_Unpressed;
                Button->Entry->ID->Color = Button->BaseColor;
                if(Button->OnHoverExit.Func) Button->OnHoverExit.Func(Button->OnHoverExit.Data);
            }
        }
    }
}

internal void
UpdateButtons(renderer *Renderer, input_info *Input)
{
    For(Renderer->ButtonGroup.Count)
    {
        if(Renderer->ButtonGroup.Buttons[It].Entry->ID->Render) 
            ButtonTestMouseInteraction(Renderer, Input,
                                       Renderer->ButtonGroup.Buttons+It);
    }
}

inline void
ActivateButton(button *Button)
{
    Button->Entry->ID->Render = true;
    Button->Icon->ID->Render = true;
    if(Button->ToggleIcon) Button->ToggleIcon->ID->Render = true;
}

inline void
DeactivateButton(button *Button)
{
    Button->Entry->ID->Render = false;
    Button->Icon->ID->Render = false;
    if(Button->ToggleIcon) Button->ToggleIcon->ID->Render = false;
}

inline void
SetButtonActive(button *Button, b32 SetActive)
{
    Button->Entry->ID->Render = SetActive;
    Button->Icon->ID->Render = SetActive;
    if(Button->ToggleIcon) Button->ToggleIcon->ID->Render = SetActive;
}

inline void
TranslateButton(button *Button, v2 Translation)
{
    Translate(Button->Entry, Translation);
    Translate(Button->Icon, Translation);
    if(Button->ToggleIcon) Translate(Button->ToggleIcon, Translation);
}

inline void
SetButtonTranslation(button *Button, v2 T)
{
    SetLocalPosition(Button->Entry, T);
    //SetLocalPosition(Button->Icon, T);
    //if(Button->ToggleIcon) SetLocalPosition(Button->ToggleIcon, T);
}

inline void
OnDraggingStart(drag_list *DragableList, renderer *Renderer, v2 MouseP)
{
    For(DragableList->Count)
    {
        if(IsInRect(DragableList->Dragables[It], MouseP))
        {
            v2 AdjustedMouseP = MouseP;
            DragableList->DraggingID = It;
            if(DragableList->OnDragStart[It].Func)
            {
                DragableList->OnDragStart[It].Func(Renderer, AdjustedMouseP, DragableList->Dragables[It], 
                                                   DragableList->OnDragStart[It].Data);
            }
        }
    }
}

inline void
OnDragging(drag_list *DragableList, renderer *Renderer, v2 MouseP)
{
    if(DragableList->OnDragging[DragableList->DraggingID].Func)
    {
        v2 AdjustedMouseP = MouseP;
        DragableList->OnDragging[DragableList->DraggingID].Func(Renderer, AdjustedMouseP, 
                                                                DragableList->Dragables[DragableList->DraggingID],
                                                                DragableList->OnDragging[DragableList->DraggingID].Data);
    }
}

inline void
OnDraggingEnd(drag_list *DragableList, renderer *Renderer, v2 MouseP)
{
    if(DragableList->OnDragEnd[DragableList->DraggingID].Func)
    {
        v2 AdjustedMouseP = MouseP;
        DragableList->OnDragEnd[DragableList->DraggingID].Func(Renderer, AdjustedMouseP, 
                                                               DragableList->Dragables[DragableList->DraggingID],
                                                               DragableList->OnDragEnd[DragableList->DraggingID].Data);
    }
    DragableList->DraggingID = -1;
}

inline v3
Color(u8 R, u8 G, u8 B)
{
    v3 Result = {};
    
    Result.r = (r32)R/255.0f;
    Result.g = (r32)G/255.0f;
    Result.b = (r32)B/255.0f;
    
    return Result;
}

// *****************************************************************************
// New render pipeline  ********************************************************
// *****************************************************************************

internal void
FixUpEntries(render_entry_list *EntryList)
{
    for(i32 It = EntryList->EntryCount-1; It >= 0; It--)
    {
        render_entry *Entry = EntryList->Entries+It;
        if(Entry->Vertice[0].z < -1.0f) 
        {
            *Entry = {};
            EntryList->EntryCount--;
        }
        else 
        {
            Entry->ID->ID = Entry;
        }
    }
}

inline i32 
GetEntryID_ID(entry_id *EntryID)
{
    i32 Result = -1;
    render_entry_list *EntryList = &GlobalGameState.Renderer.RenderEntryList;
    
    For(EntryList->MaxCount)
    {
        if(EntryID->ID == EntryList->IDs[It].ID)
        {
            Result = It;
            break;
        }
    }
    Assert(Result >= 0);
    
    return Result;
}

internal void
RemoveRenderEntry(entry_id *EntryID)
{
    render_entry_list *EntryList = &GlobalGameState.Renderer.RenderEntryList;
    Assert(EntryID->ID >= 0);
    i32 EntryID_ID = GetEntryID_ID(EntryID);
    if(!EntryList->OpenSlots[EntryID_ID])
    {
        EntryList->OpenSlots[EntryID_ID] = true;
        EntryList->OpenSlotCount++;
        
        EntryID->ID->Vertice[0].z = -2; // If this is set, it will be deleted on next sort
        EntryID->ID = 0;
        
        EntryList->_SortingNeeded = true;
    }
}
internal entry_id *
CreateRenderEntry(renderer *Renderer, v2 Size, r32 Depth, v2 Position = {}, entry_id *Parent = 0)
{
    entry_id *Result = 0;
    
    render_entry_list *EntryList = &Renderer->RenderEntryList;
    if(EntryList->OpenSlotCount > 0)
    {
        For(EntryList->MaxCount)
        {
            if(EntryList->OpenSlots[It])
            {
                Result = EntryList->IDs + It; 
                EntryList->OpenSlots[It] = false;
                EntryList->OpenSlotCount--;
                Assert(Result->ID == 0);
                break;
            }
        }
    }
    else
    {
        Assert(EntryList->IDCount < EntryList->MaxCount);
        Result = EntryList->IDs + EntryList->IDCount++;
    }
    Assert(Result);
    Assert(Depth <= 1.0f && Depth >= -1.0f);
    Assert(EntryList->EntryCount < EntryList->MaxCount);
    
    render_entry *Entry = EntryList->Entries + EntryList->EntryCount;
    Entry->ID = Result;
    Result->ID = Entry;
    EntryList->EntryCount++;
    EntryList->_SortingNeeded = true;
    
    Assert(Result->ID >= 0);
    Assert(Entry);
    
    
    Entry->Type = renderType_NONE;
    Entry->Render = true;
    Entry->Transform = {};
    Entry->Transform.Scale = {1,1};
    Entry->Transform.Translation = Position;
    Size *= 0.5f;
    Entry->Vertice[0] = {-Size.x, -Size.y, Depth};
    Entry->Vertice[1] = {-Size.x,  Size.y, Depth};
    Entry->Vertice[2] = { Size.x,  Size.y, Depth};
    Entry->Vertice[3] = { Size.x, -Size.y, Depth};
    Entry->TexCoords[0] = {0, 0};
    Entry->TexCoords[1] = {0, 1};
    Entry->TexCoords[2] = {1, 1};
    Entry->TexCoords[3] = {1, 0};
    Entry->Parent = Parent;
    Entry->Transparency = 1.0f;
    
    return Result;
}

inline entry_id *
CreateRenderRect(renderer *Renderer, rect Rect, r32 Depth, entry_id *Parent, v3 *Color)
{
    entry_id *Result = 0;
    
    rect_pe_2D RectPE = RectToRectPE(Rect);
    Result = CreateRenderEntry(Renderer, RectPE.Extends*2, Depth, RectPE.Pos, Parent);
    
    Result->ID->Type = renderType_2DRectangle;
    Result->ID->Color = Color;
    
    return Result;
}

internal entry_id *
CreateRenderRect(renderer *Renderer, v2 Size, r32 Depth, v3 *Color, entry_id *Parent)
{
    entry_id *Result = CreateRenderEntry(Renderer, Size, Depth, {}, Parent);
    
    Result->ID->Type = renderType_2DRectangle;
    Result->ID->Color = Color;
    
    return Result;
}

inline entry_id *
CreateRenderBitmap(renderer *Renderer, rect Rect, r32 Depth, entry_id *Parent, loaded_bitmap Bitmap)
{
    entry_id *Result = 0;
    
    rect_pe_2D RectPE = RectToRectPE(Rect);
    Result = CreateRenderEntry(Renderer, RectPE.Extends*2, Depth, RectPE.Pos, Parent);
    
    Result->ID->Type = renderType_2DBitmap;
    Result->ID->TexID = CreateGLTexture(Bitmap);
    
    return Result;
}

inline entry_id *
CreateRenderBitmap(renderer *Renderer, rect Rect, r32 Depth, entry_id *Parent, u32 BitmapID)
{
    entry_id *Result = 0;
    
    rect_pe_2D RectPE = RectToRectPE(Rect);
    Result = CreateRenderEntry(Renderer, RectPE.Extends*2, Depth, RectPE.Pos, Parent);
    
    Result->ID->Type = renderType_2DBitmap;
    Result->ID->TexID = BitmapID;
    
    return Result;
}

internal entry_id *
CreateRenderBitmap(renderer *Renderer, v2 Size, r32 Depth, entry_id *Parent, u32 BitmapID)
{
    entry_id *Result = 0;
    
    Result = CreateRenderEntry(Renderer, Size, Depth, {}, Parent);
    
    Result->ID->Type = renderType_2DBitmap;
    Result->ID->TexID = BitmapID;
    
    return Result;
}

inline entry_id *
CreateRenderBitmap(renderer *Renderer, rect Rect, r32 Depth, entry_id *Parent, string_c *Path)
{
    entry_id *Result = 0;
    
    rect_pe_2D RectPE = RectToRectPE(Rect);
    Result = CreateRenderEntry(Renderer, RectPE.Extends*2, Depth, RectPE.Pos, Parent);
    
    loaded_bitmap Bitmap = LoadImage_STB(Path->S); 
    Assert(Bitmap.Pixels);
    
    Result->ID->Type = renderType_2DBitmap;
    Result->ID->TexID = CreateGLTexture(Bitmap);
    FreeImage_STB(Bitmap);
    return Result;
}

inline void
SetTransparency(entry_id *Entry, r32 T)
{
    Entry->ID->Transparency = Clamp01(T);
}

// Transform helper *****************************************************************


inline v2
GetSize(entry_id *Entry)
{
    v2 Result = HadamardProduct(GetExtends(Entry->ID->Vertice)*2, Entry->ID->Transform.Scale);
    return Result;
}

inline void
SetSize(entry_id *Entry, v2 SizeInPixel)
{
    Entry->ID->Transform.Scale = HadamardDivision(SizeInPixel, GetExtends(Entry->ID->Vertice)*2);
}

inline v2
GetScale(entry_id *Entry)
{
    v2 Result = Entry->ID->Transform.Scale;
    return Result;
}

inline void
SetScale(entry_id *Entry, v2 Scale)
{
    Entry->ID->Transform.Scale = Scale;
}

inline v2
GetExtends(v3 *RenderRectVertice)
{
    v2 Result = RenderRectVertice[2].xy;
    return Result;
}

inline v2
GetExtends(entry_id *Entry)
{
    return GetExtends(Entry->ID->Vertice);
}

inline rect 
GetRect(entry_id *Entry)
{
    rect Result = {};
    
    v2 P = GetPosition(Entry);
    v2 E = GetExtends(Entry);
    Result.Min = P - E;
    Result.Max = P + E;
    
    return Result;
}

inline v2
GetPosition(entry_id *Entry)
{
    v2 Result = {};
    while(Entry)
    {
        Result += Entry->ID->Transform.Translation;
        Entry = Entry->ID->Parent;
    }
    return Result;
}

inline void
SetPosition(entry_id *Entry, v2 NewTranslation)
{
    v2 ParentTranslation = (Entry->ID->Parent) ? GetPosition(Entry->ID->Parent) : V2(0);
    
    Entry->ID->Transform.Translation = NewTranslation - ParentTranslation;
}

inline v2
GetLocalPosition(entry_id *Entry)
{
    v2 Result = Entry->ID->Transform.Translation;
    return Result;
}

inline void
SetLocalPosition(entry_id *Entry, v2 NewTranslation)
{
    Entry->ID->Transform.Translation = NewTranslation;
}

inline void
Translate(entry_id *Entry, v2 TranslationOffset)
{
    Entry->ID->Transform.Translation += TranslationOffset;
}

// Entry rect relation helper *********************************************************

inline r32
HeightBetweenRects(entry_id *RectA, entry_id *RectB)
{
    r32 Result = 0;
    v3 VA[4];
    ApplyTransform(RectA->ID, VA);
    v3 VB[4];
    ApplyTransform(RectB->ID, VB);
    
    if(VA[0].y > VB[2].y)
    {
        Result = Abs(VB[2].y - VA[0].y);
    }
    else if(VA[2].y <= VB[0].y)
    {
        Result = Abs(VA[2].y - VB[0].y);
    }
    
    return Result;
}

inline r32
WidthBetweenRects(entry_id *RectA, entry_id *RectB)
{
    r32 Result = 0;
    v3 VA[4];
    ApplyTransform(RectA->ID, VA);
    v3 VB[4];
    ApplyTransform(RectB->ID, VB);
    
    if(VA[0].x > VB[2].x)
    {
        Result = Abs(VB[2].x - VA[0].x);
    }
    else if(VA[2].x <= VB[0].x)
    {
        Result = Abs(VA[2].x - VB[0].x);
    }
    
    return Result;
}

inline r32
CenterXBetweenRects(entry_id *LeftRect, entry_id *RightRect)
{
    r32 Result = {};
    v3 VA[4];
    ApplyTransform(LeftRect->ID, VA);
    v3 VB[4];
    ApplyTransform(RightRect->ID, VB);
    
    Result = VA[2].x + (VB[0].x - VA[2].x)*0.5f;
    
    return Result;
}

inline r32
CenterYBetweenRects(entry_id *BottomRect, entry_id *TopRect)
{
    r32 Result = {};
    v3 VA[4];
    ApplyTransform(BottomRect->ID, VA);
    v3 VB[4];
    ApplyTransform(TopRect->ID, VB);
    
    Result = VA[2].y + (VB[0].y - VA[2].y)*0.5f;
    
    return Result;
}

inline b32
IsLowerThanRect(entry_id *Entry, entry_id *Rect)
{
    b32 Result = false;
    v3 VE[4];
    ApplyTransform(Entry->ID, VE);
    v3 VR[4];
    ApplyTransform(Rect->ID, VR);
    
    if(VE[2].y < VR[0].y)
    {
        Result = true;
    }
    
    return Result;
}

inline b32
IsHigherThanRect(entry_id *Entry, entry_id *Rect)
{
    b32 Result = false;
    v3 VE[4];
    ApplyTransform(Entry->ID, VE);
    v3 VR[4];
    ApplyTransform(Rect->ID, VR);
    
    if(VE[0].y > VR[2].y)
    {
        Result = true;
    }
    
    return Result;
}

inline b32
IsTopShowing(entry_id *Entry, entry_id *Rect)
{
    b32 Result = false;
    v3 VE[4];
    ApplyTransform(Entry->ID, VE);
    v3 VR[4];
    ApplyTransform(Rect->ID, VR);
    
    if(VE[2].y > VR[2].y)
    {
        Result = true;
    }
    
    return Result;
}

inline b32
IsBottomShowing(entry_id *Entry, entry_id *Rect)
{
    b32 Result = false;
    v3 VE[4];
    ApplyTransform(Entry->ID, VE);
    v3 VR[4];
    ApplyTransform(Rect->ID, VR);
    
    if(VE[0].y < VR[0].y)
    {
        Result = true;
    }
    
    return Result;
}

inline b32
IsIntersectingRectButTopShowing(entry_id *Entry, entry_id *Rect)
{
    b32 Result = false;
    v3 VE[4];
    ApplyTransform(Entry->ID, VE);
    v3 VR[4];
    ApplyTransform(Rect->ID, VR);
    
    if(VE[0].y < VR[2].y && 
       VE[2].y > VR[2].y)
    {
        Result = true;
    }
    
    return Result;
}

inline b32
IsIntersectingRectButBottomShowing(entry_id *Entry, entry_id *Rect)
{
    b32 Result = false;
    v3 VE[4];
    ApplyTransform(Entry->ID, VE);
    v3 VR[4];
    ApplyTransform(Rect->ID, VR);
    
    if(VE[2].y > VR[0].y && 
       VE[0].y < VR[0].y)
    {
        Result = true;
    }
    
    return Result;
}

inline b32
IsInRect(rect_2D Rect, v2 P)
{
    b32 Result = false;
    
    if(P.x < Rect.Max.x && P.x > Rect.Min.x &&
       P.y < Rect.Max.y && P.y > Rect.Min.y)
    {
        Result = true;
    }
    
    return Result;
}

inline b32
IsInRect(entry_id *Entry, v2 P)
{
    b32 Result = false;
    
    rect_2D Rect = ExtractScreenRect(Entry);
    Result = IsInRect(Rect, P);
    
    return Result;
}

inline rect
ExtractScreenRect(entry_id *Entry)
{
    rect Result = {};
    
    v3 AdjustedCorners[4];
    ApplyTransform(Entry->ID, AdjustedCorners);
    
    Result.Min = AdjustedCorners[0].xy;
    Result.Max = AdjustedCorners[2].xy;
    
    return Result;
}

inline r32
DistanceToRectEdge(entry_id *Entry, v2 Point)
{
    r32 Result = MAX_REAL32;
    
    rect Rect = ExtractScreenRect(Entry);
    
    v2 Smaller = Rect.Min - Point;
    v2 Bigger = Point - Rect.Max;
    
    if(Smaller.x > 0) Result = Smaller.x;
    if(Smaller.y > 0 && Smaller.y < Result) Result = Smaller.y;
    if(Bigger.x  > 0 && Bigger.x  < Result)  Result = Bigger.x;
    if(Bigger.y  > 0 && Bigger.y  < Result)  Result = Bigger.y;
    if(Result == MAX_REAL32) Result = 0;
    
    return Result;
}

// Screen size change transform *************************************************

inline screen_transform_list
CreateScreenTransformList(memory_bucket_container *Bucket, u32 Size)
{
    screen_transform_list Result = {};
    Result.Entries       = PushArrayOnBucket(Bucket, Size, entry_id *);
    Result.FixToPosition = PushArrayOnBucket(Bucket, Size, v2);
    Result.OriginalPosition = PushArrayOnBucket(Bucket, Size, v2);
    Result.DoTranslation = PushArrayOnBucket(Bucket, Size, fixed_to);
    Result.DoScale       = PushArrayOnBucket(Bucket, Size, scale_axis);
    Result.OpenSlots     = PushArrayOnBucket(Bucket, Size, b32);
    Result.MaxCount      = Size;
    
    return Result;
}

inline void
RemoveFromTransformList(screen_transform_list *List, entry_id *Entry)
{
    For(List->Count)
    {
        if(List->Entries[It] == Entry)
        {
            List->OpenSlots[It] = true;
            List->OpenSlotCount++;
            break;
        }
    }
}

inline u32
TransformWithScreen(screen_transform_list *List, entry_id *Entry, fixed_to FixedTo, scale_axis ScaleAxis, v2 FixToPosition)
{
    i32 ID = -1;
    if(List->OpenSlotCount > 0)
    {
        For(List->Count)
        {
            if(List->OpenSlots[It])
            {
                ID = It;
                List->OpenSlots[It] = false;
                List->OpenSlotCount--;
                break;
            }
        }
    }
    else 
    {
        Assert(List->Count < List->MaxCount);
        ID = List->Count++;
    }
    
    List->Entries[ID]          = Entry;
    List->FixToPosition[ID]    = FixToPosition;
    List->OriginalPosition[ID] = GetPosition(Entry);
    List->DoTranslation[ID]    = FixedTo;
    List->DoScale[ID]          = ScaleAxis;
    return ID;
}

inline u32 
TransformWithScreen(screen_transform_list *List, entry_id *Entry, fixed_to FixedTo, scale_axis ScaleAxis, r32 FixToPosition)
{
    return TransformWithScreen(List, Entry, FixedTo, ScaleAxis, {FixToPosition, FixToPosition});
}

inline u32 
TranslateWithScreen(screen_transform_list *List, entry_id *Entry, fixed_to FixedTo, r32 FixToPosition)
{
    return TransformWithScreen(List, Entry, FixedTo, scaleAxis_None, {FixToPosition, FixToPosition});
}

inline u32 
TranslateWithScreen(screen_transform_list *List, entry_id *Entry, fixed_to FixedTo, v2  FixToPosition)
{
    return TransformWithScreen(List, Entry, FixedTo, scaleAxis_None, FixToPosition);
}

inline u32
ScaleWithScreen(screen_transform_list *List, entry_id *Entry, scale_axis ScaleAxis)
{
    return TransformWithScreen(List, Entry, fixedTo_None, ScaleAxis);
}

inline void 
ChangeFixToPosition(screen_transform_list *List, u32 ID, r32 NewFixToPosition)
{
    Assert(ID < List->Count);
    List->FixToPosition[ID].x = NewFixToPosition;
    List->FixToPosition[ID].y = NewFixToPosition;
}

inline void 
ChangeFixToPosition(screen_transform_list *List, u32 ID, v2 NewFixToPosition)
{
    Assert(ID < List->Count);
    List->FixToPosition[ID] = NewFixToPosition;
}

internal void
PerformScreenTransform(renderer *Renderer)
{
    v2 FixedDim   = V2(Renderer->Window.FixedDim.Dim);
    v2 CurrentDim = V2(Renderer->Window.CurrentDim.Dim);
    
    screen_transform_list *List = &Renderer->TransformList;
    
    For(List->Count)
    {
        if(List->OpenSlots[It]) continue;
        Assert(List->DoScale[It] != scaleAxis_None || List->DoTranslation[It] != fixedTo_None);
        entry_id *Entry = List->Entries[It];
        if(List->DoTranslation[It] != fixedTo_None)
        {
            Entry->ID->FixedTo = List->DoTranslation[It];
            v2 Center = List->OriginalPosition[It];
            v2 AnchorP = {0.5f, 0.5f};
            switch(List->DoTranslation[It])
            {
                case fixedTo_Center: break;
                case fixedTo_BottomLeft:      AnchorP = {0.0f, 0.0f}; break;
                case fixedTo_BottomRight:     AnchorP = {1.0f, 0.0f}; break;
                case fixedTo_BottomCenter:    AnchorP = {0.5f, 0.0f}; break;
                case fixedTo_TopLeft:         AnchorP = {0.0f, 1.0f}; break;
                case fixedTo_TopRight:        AnchorP = {1.0f, 1.0f}; break;
                case fixedTo_TopCenter:       AnchorP = {0.5f, 1.0f}; break;
                case fixedTo_LeftCenter:      AnchorP = {0.0f, 0.5f}; break;
                case fixedTo_RightCenter:     AnchorP = {1.0f, 0.5f}; break;
                case fixedTo_FixXToGiven_YBottom: AnchorP = {List->FixToPosition[It].x, 0.0f}; break;   
                case fixedTo_FixXToGiven_YCenter: AnchorP = {List->FixToPosition[It].x, 0.5f}; break;
                case fixedTo_FixXToGiven_YTop:    AnchorP = {List->FixToPosition[It].x, 1.0f}; break;
                case fixedTo_FixYToGiven_XLeft:   AnchorP = {0.0f, List->FixToPosition[It].y}; break;
                case fixedTo_FixYToGiven_XCenter: AnchorP = {0.5f, List->FixToPosition[It].y}; break;
                case fixedTo_FixYToGiven_XRight:  AnchorP = {1.0f, List->FixToPosition[It].y}; break;
                case fixedTo_FixXYToGiven:        AnchorP = List->FixToPosition[It]; break;
                InvalidDefaultCase;
            }
            
            v2 CurrentCenter = HadamardProduct(AnchorP, CurrentDim);
            v2 FixedCenter   = HadamardProduct(AnchorP, FixedDim);
            v2 Result        = CurrentCenter - (FixedCenter - Center);
            
            Entry->ID->Transform.Translation = Result;// - Center;
        }
        
        if(List->DoScale[It] != scaleAxis_None)
        {
            v2 NewSize = GetExtends(Entry)*2;
            if(List->DoScale[It] == scaleAxis_X ||
               List->DoScale[It] == scaleAxis_XY)
            {
                r32 LeftDist  = Entry->ID->Vertice[0].x;
                r32 RightDist = FixedDim.x - Entry->ID->Vertice[2].x;
                NewSize.x = Max(0.0f, CurrentDim.x - (LeftDist + RightDist));
            }
            if(List->DoScale[It] == scaleAxis_Y ||
               List->DoScale[It] == scaleAxis_XY)
            {
                r32 DownDist  = Entry->ID->Vertice[0].y;
                r32 UpDist    = FixedDim.y - Entry->ID->Vertice[2].y;
                NewSize.y = Max(0.0f, CurrentDim.y - (DownDist + UpDist));
            }
            SetSize(Entry, NewSize);
        }
    }
}

// Render Text********************************************************

internal render_entry
CreateRenderTextEntry(renderer *Renderer, v2 Extends, r32 Depth, u32 BitmapID, v3 *Color, entry_id *Parent = 0)
{
    render_entry Result = {};
    
    Result.Type = renderType_Text;
    Result.Render = true;
    Result.Transform = {};
    Result.Transform.Scale = {1,1};
    Result.Vertice[0] = {-Extends.x, -Extends.y, Depth};
    Result.Vertice[1] = {-Extends.x,  Extends.y, Depth};
    Result.Vertice[2] = { Extends.x,  Extends.y, Depth};
    Result.Vertice[3] = { Extends.x, -Extends.y, Depth};
    Result.Parent = Parent;
    Result.Transparency = 1.0f;
    Result.Color = Color;
    Result.TexID = BitmapID;
    
    return Result;
}

internal void
CreateRenderText(renderer *Renderer, render_text_atlas *Atlas, string_c *Text, 
                 v3 *Color, render_text *ResultText, r32 ZValue, entry_id *Parent, v2 StartP)
{
    ResultText->Count = 0;
    if(Parent) StartP += GetPosition(Parent);
    ResultText->StartP = StartP;
    ResultText->CurrentP = StartP;
    r32 DepthOffset = ZValue;
    
    ResultText->Base = CreateRenderBitmap(Renderer, V2(0), DepthOffset, Parent, Atlas->GLID);
    SetPosition(ResultText->Base, StartP);
    ResultText->Base->ID->Type = renderType_Text;
    ResultText->Base->ID->Text = ResultText;
    Parent = ResultText->Base;
    
    if(ResultText->MaxCount == 0)
    {
        ResultText->MaxCount         = Max(CHARACTERS_PER_TEXT_INFO, Text->Pos+1);
        ResultText->RenderEntries    = PushArrayOnBucket(&GlobalGameState.Bucket.Fixed, ResultText->MaxCount, render_entry);
        ResultText->StartPointOffset = PushArrayOnBucket(&GlobalGameState.Bucket.Fixed, ResultText->MaxCount, v2);
    }
    // TODO:: Maybe when this happens increase the size? With current allocater system
    // this would not work very well. But once it changed... maybe.
    Assert(Text->Pos < ResultText->MaxCount); 
    
    // Calculate baseline offset positions
    v2 TP = StartP;
    stbtt_aligned_quad TestQ;
    stbtt_GetBakedQuad(Atlas->CharData, Atlas->Bitmap.Width, Atlas->Bitmap.Height, 'o'-32, &TP.x, &TP.y, &TestQ, 1);
    r32 NewBaseline = TestQ.y0;
    r32 OldBaseline = TestQ.y1;
    
    v2 BaseP = {};
    For(Text->Pos)
    {
        u8 NextSymbol = Text->S[It];
        if(Text->S[It] >= 128)
        {
            for(i32 SymbolID = 0; SymbolID < ArrayCount(BasicSymbolsGer); SymbolID++)
            {
                if(CompareStringAndCompound(&BasicSymbolsGer[SymbolID].UTF8, Text->S+It))
                {
                    NextSymbol = BasicSymbolsGer[SymbolID].ANSI;
                    It++;
                }
            }
        }
        if(NextSymbol >= 32 && NextSymbol < ATLAS_LETTER_COUNT)
        {
            stbtt_aligned_quad A;
            stbtt_GetBakedQuad(Atlas->CharData, 
                               Atlas->Bitmap.Width, Atlas->Bitmap.Height, NextSymbol-32,
                               &ResultText->CurrentP.x, &ResultText->CurrentP.y, &A, 1);
            
            rect Rect = {{A.x0, A.y0}, {A.x1, A.y1}};
            rect_pe RectPE = RectToRectPE(Rect);
            ResultText->RenderEntries[ResultText->Count] = CreateRenderTextEntry(Renderer, RectPE.Extends, DepthOffset, 
                                                                                 Atlas->GLID, Color, Parent);
            render_entry *Entry  = ResultText->RenderEntries+ResultText->Count++;
            entry_id EntryID = {Entry};
            SetPosition(&EntryID, GetCenter(Rect));
            
            r32 DistToBaseline = GetRect(&EntryID).Max.y - OldBaseline;
            r32 BaselineOffset = GetRect(&EntryID).Min.y - NewBaseline;
            Translate(&EntryID, V2(0, -(BaselineOffset + DistToBaseline)));
            
            Entry->TexCoords[0] = {A.s0, A.t1};
            Entry->TexCoords[1] = {A.s0, A.t0};
            Entry->TexCoords[2] = {A.s1, A.t0};
            Entry->TexCoords[3] = {A.s1, A.t1};
            
            DepthOffset -= 0.000001f;
        }
        if(NextSymbol == 10)
        {
            ResultText->CurrentP = V2(StartP.x, ResultText->CurrentP.y + (OldBaseline-NewBaseline)*2);
        }
    }
}

inline void
SetPosition(render_text *Text, v2 P)
{
    if(Text->Base == 0) return;
    SetPosition(Text->Base, P);
}

inline void
SetLocalPosition(render_text *Text, v2 P)
{
    if(Text->Base == 0) return;
    SetLocalPosition(Text->Base, P);
}

inline void
SetPositionX(render_text *Text, r32 X)
{
    if(Text->Base == 0) return;
    SetPosition(Text->Base, V2(X, GetPosition(Text->Base).y));
}

inline void
SetPositionY(render_text *Text, r32 Y)
{
    if(Text->Base == 0) return;
    SetPosition(Text->Base, V2(GetPosition(Text->Base).x, Y));
}

inline void
Translate(render_text *Text, v2 Translation)
{
    if(Text->Base == 0) return;
    Translate(Text->Base, Translation);
}

inline v2
GetPosition(render_text *Text, u32 LetterID)
{
    Assert(LetterID < Text->Count);
    v2 Result = {};
    entry_id EntryID = {Text->RenderEntries+LetterID};
    Result = GetPosition(&EntryID);
    return Result;
}

inline void
SetRenderText(render_text *Text, b32 Render)
{
    if(Text->Base == 0) return;
    Text->Base->ID->Render = Render;
}

inline void 
RemoveRenderText(render_text *Text)
{
    Text->Count = 0;
    if(Text->Base) 
    {
        RemoveRenderEntry(Text->Base);
        Text->Base = 0;
    }
}

inline void
SetTransparency(render_text *Text, r32 T)
{
    For(Text->Count)
    {
        Text->RenderEntries[It].Transparency = Clamp01(T);
    }
}

inline void 
SetColor(render_text *Text, v3 *Color)
{
    For(Text->Count)
    {
        Text->RenderEntries[It].Color = Color;
    }
}

// *******************  Sorting algorithms ***********************
// 3-Way Quicksort ***********************************************

#define GetDepth3(Entry) (Entry).Vertice[0].z

inline void
Swap(render_entry *A, render_entry *B)
{
    render_entry T = *A;
    *A = *B;
    *B = T;
}

// This function partitions a[] in three parts 
// a) a[l..i] contains all elements smaller than pivot 
// b) a[i+1..j-1] contains all occurrences of pivot 
// c) a[j..r] contains all elements greater than pivot
internal void
Partition(render_entry *Entries, i32 l, i32 r, i32 *i, i32 *j) 
{ 
    *i = l-1;
    *j = r; 
    i32 p = l-1;
    i32 q = r; 
    r32 v = GetDepth3(Entries[r]); 
    
    while (true) 
    { 
        // From left, find the first element smaller than 
        // or equal to v. This loop will definitely terminate 
        // as v is last element 
        while (GetDepth3(Entries[++(*i)]) > v) ; 
        
        // From right, find the first element greater than or 
        // equal to v 
        while (v > GetDepth3(Entries[--(*j)])) 
            if (*j == l) 
            break; 
        
        // If i and j cross, then we are done 
        if (*i >= *j) break; 
        
        // Swap, so that smaller goes on left greater goes on right 
        Swap(Entries+ *i, Entries+ *j); 
        
        // Move all same left occurrence of pivot to beginning of 
        // array and keep count using p 
        if (GetDepth3(Entries[*i]) == v) 
        { 
            p++; 
            Swap(Entries + p, Entries + *i); 
        } 
        
        // Move all same right occurrence of pivot to end of array 
        // and keep count using q 
        if (GetDepth3(Entries[*j]) == v) 
        { 
            q--; 
            Swap(Entries + *j, Entries + q); 
        } 
    } 
    
    // Move pivot element to its correct index 
    Swap(Entries+*i, Entries+r); 
    
    // Move all left same occurrences from beginning 
    // to adjacent to arr[i] 
    *j = (*i)-1; 
    for (i32 k = l; k < p; k++, (*j)--) 
        Swap(Entries+k, Entries+*j); 
    
    // Move all right same occurrences from end 
    // to adjacent to arr[i] 
    *i = (*i)+1; 
    for (i32 k = r-1; k > q; k--, (*i)++) 
        Swap(Entries+*i, Entries+k); 
} 

// 3-way partition based quick sort 
inline void 
Quicksort3Recurse(render_entry *Entries, i32 l, i32 r) 
{ 
    if (r <= l) return; 
    
    i32 i, j; 
    
    // Note that i and j are passed as reference 
    Partition(Entries, l, r, &i, &j); 
    
    // Recur 
    Quicksort3Recurse(Entries, l, j); 
    Quicksort3Recurse(Entries, i, r); 
}

inline void 
Quicksort3(render_entry *Entries, i32 Count) 
{
    Quicksort3Recurse(Entries, 0, Count-1);
}

inline void 
SuppressRenderEntrySorting(renderer *Renderer, b32 DoSuppress)
{
    Renderer->RenderEntryList.SuppressSorting = DoSuppress;
}

inline void
ReadyUpEntryList(render_entry_list *EntryList)
{
    if(EntryList->_SortingNeeded) 
    {
        EntryList->_SortingNeeded = false;
        Quicksort3(EntryList->Entries, EntryList->EntryCount);
        FixUpEntries(EntryList);
    }
}





