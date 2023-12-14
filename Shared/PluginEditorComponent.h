#pragma once
#include <JuceHeader.h>


//==============================================================================
struct PluginEditorComponent     : public Component
{  
    using ProcEditor             = std::unique_ptr<AudioProcessorEditor>;
    using GridLayouFn            = std::function<Grid(Component*)>;

    PluginEditorComponent(ProcEditor editorIn, GridLayouFn layoutIn = nullptr)
    : editor (std::move (editorIn)), layout (std::move (layoutIn))
    {
        addAndMakeVisible (editor.get());
        childBoundsChanged (editor.get());
    }

    void childBoundsChanged (Component* child) override
    {
        if (child != editor.get()) 
            return;
        
        else if (child == editor.get())
        {
            auto size = editor.get()->getBounds();

            if (!layout) 
            {
                setSize(size.getWidth(), size.getHeight());
                return;
            }

            else if (layout)
            {
                auto grid = layout(editor.get());

                for (const auto& item : grid.items)
                    if (item.associatedComponent != editor.get())
                        if (auto comp = item.associatedComponent; !comp->isVisible()) 
                                addAndMakeVisible(comp);

                auto [w, h] = calculateGridComponentSizes(grid);

                setSize((w + size.getWidth()), (h + size.getHeight()));
                grid.performLayout(getBounds());
            }
        }
    }

    void setScaleFactor (float scale)
    {
        if (editor != nullptr) 
            editor->setScaleFactor (scale);
    }

    void setLayout (GridLayouFn func)
    {
        layout = std::move (func);
        childBoundsChanged (editor.get());
    }

private:
    float getTotalAbsoluteSize (const Array<Grid::TrackInfo>& tracks, Grid::Px gapSize) noexcept
    {
        float totalCellSize = 0.0f;

        for (const auto& trackInfo : tracks)
            if (! trackInfo.isFractional() || trackInfo.isAuto())
                totalCellSize += trackInfo.getSize();

        float totalGap = tracks.size() > 1 ? static_cast<float> ((tracks.size() - 1) * gapSize.pixels)
                                           : 0.0f;
        return totalCellSize + totalGap;
    }

    std::pair<int, int> calculateGridComponentSizes(const Grid& grid)
    {
        return { getTotalAbsoluteSize(grid.templateColumns, grid.columnGap), 
                    getTotalAbsoluteSize(grid.templateRows, grid.rowGap) };
    }

    ProcEditor  editor;
    GridLayouFn layout;
};



//==============================================================================
struct ScaledDocumentWindow     : public DocumentWindow
{
    ScaledDocumentWindow (String title, Colour bg, float scale) 
    : DocumentWindow (title, bg, 7), desktopScale (scale) {}

    float getDesktopScaleFactor() const override 
    {
        return Desktop::getInstance().getGlobalScaleFactor() * desktopScale; 
    }

    void closeButtonPressed() override 
    {
        if (onCloseButtonPressed) onCloseButtonPressed(); 
    }

    std::function<void()> onCloseButtonPressed;

private:
    float desktopScale = 1.0f;
};
