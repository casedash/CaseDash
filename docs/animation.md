# Animation

The following elements of the ui are going to be animated:
- Bars in metrics and drive usage
- Gauge display widgets
- Charts in throughput widget

The goal is to have those visual displays animates so that it feels smooth in between 500 ms metrics updates, not jittery, but keep the CPU consumption very low. Thus we don't animate any text changes which are expensive to paint. The text is still going to be updated every 500 ms on metrics update. After text updated, 
the actual visual value change is animated from the old the new value over the 
course of the next 500 ms.

The drawing of the up is split into three layers:
- Snapshot layer contains backgrounds, card chrome, all the text and background for animated elements (bar and gauge tracks, chart background). It updates on all metrics updates and on all layout changes.
- Animation layer contains animated elements. It is redrawn on each animation frame. 
- Overlay layer (optional) contains all overlays that are drawn on top of animaitons. This includes layout edit guides and anchors, edit dialog highlihghts, and move overlay. It is redrawn on all edit layout changes, including mouse drags. The overlay layer is used and crated only when there are overlays to show. In the normal operation it is not even allocated. 

Threading architecture:
- Metrics update thread is responsible for updaing metrics every 500 ms and ships updates the main thread.
- Main thread is processing all mouse events and metrics update. Redraws snapshot layer and overlay layer into in-memory bitmaps and ships them to the render thread. Note that snapshot is not transparent, while overlay is rendered with transparensy.
- Render is responsible for actually presenting the frame to device. It uses DXGI flip-model swap chain to sync the actual frame to vsync, takes pre-rendered snapshot layout, draws the current state of animaion on top of it, then blts the overlay on top, and presents the frame.

## Code architecture

- The widget module ecapsulates the geometry of the shapes and animation. The actuall paint code of the widgets is split. The widget's draw paints the snasphost layer and produces a list of animations. Note, that this call is happening in the main thread.
- The animations are represented by the interface which encapsulates the actualy draw logaic. Which there are 4 widgets that do animations (drive_usage, metric_list, gauage, throughput), there are only 3 actual animation implementations for animations: pill bar, gauge, and chart. 
- The animation contains the snapshot of all the geometry -- the bounding box of the animation and all the other geometric parameters that are needed for it to be drawn. It does not keep the reference to the widget nor to configuration object, so that it can be safelly shipped to the render thread, while the main thread is free to concurrently modify anything.
- The animation object keep the actual values to be draw separately, under a separate type-erased interface. There are actually only two kinds of data objects:   
   - one for keeping bar and gaguage data which consists of two fractions: for value and for max ghost. Name TBD.
   - one for keeping char bars data.
- The render thread own and remember the previous data it was provided with. When new animation objects arrive it now has old and new copy of data, which it uses to linearly interpolate animation between old and new data points for 500 ms and draw the interpolated data on each frame.
- If metric updates arrive to the render thread before 500 ms of animation has elapsed, the render thread that the currently interpolated value between old pair of old+new data as the old data, so that animation still continues in a smooth way.
- In order to identify the data, the key consisting of widget type and metrics name is used. Widgets may idependently produce the same data inside different animation objects. Render thread may (but does not have to) depulicate this animated data by key in order to compute interpolated values for drawing. 

## Animation interpolation details

- Bar and gauge data are linearly interpolated. The care shall only be take for NaN that might happen in place of missing values. 
- Chart data contains the horizontal phase shift and the computed max value for chart. The goal is that all of the following is smoothly animated: horizontal shift of the chart on new data arriving to the right, change of vertical scale when chart maximum changes, and leader up/down movement to the last value. The actual data design for chart data is TBD.

## Interaction with layout edit drag

During layout edit rows in metrics list can be dragged up/down and the whole widgets or layout contrainers can be dragged up/down or left/right with mouse. This whole interaction is orhestrated by the main thread, which draws the updates snapshot layer and shipts update the resulting bitmaps of snapshot and overlay layers to the render thread, inlucindg updated animations geometry. However, this may create a situation where a rectangle is being dragged over underling widgets and on top of animations displayed there. It is the task of the main tread to check for intersections between underlying animation's rectanged and dragged-over-object and clip the bounding boxes of the underlying animation. It is the responsibility of the render thread to respect the clipping rectangles when drawing the animation. 

Note, that render thread also uses those animation clipping rectangles to efficiently present animation to DXGI flip chain, notify which areas need to change on animation and avoid repaining of the whole dashboard window on animation.

## Other architectural considerations

- Direct2D engine has to be created in multithreaded mode, as multiple threads (main and render) will use it.
- Cached palette will have to have a separate instance for main and render threads, since main thread might be modifying palette while render thread is still redering with the old one.
- The render thread owners HWND and the actual device. The API to set HWND shall not be publically exposed outside of dashboard_renderer module, only the API of the render thread.
- The main thread post updates to the reder thread, but does not queue them. The most recent update overwrites the previous one if it was not picked up by render thread yet (it may happen on fast mouse-drags during layout edits, for example)
- In order for render thread to know what changed, the main thread provides separate versions to its updates: version of snapshot bitmap, version of the metrics, version of the palette. The render thread compares with the last known version to know what to update. The important case is when layout is changing (due to edit), so snapshot update and new animations geometry is being delivered, but metrics did not change, the render thread continues to animate metrics on its current animation time, keeping the old data values from the previous metrics update.
- Bitmaps are not allocated/freed at will, but use a shared pool. Main thread takes from the pool the fresh one to paint (it gets created only when there not bitmap in the pool), publishes rendered bitmaps of snapshot/overlay layers for render thread (if old published object gets replace its bitmaps go back to pool), then render thread returns bitmaps to pool when it finished with the frame.
- A care needs to be take to syncrhonize structural changes (windows size) between the main thread and render thread. Design TBD.

## Detailed module ownership

TBD
