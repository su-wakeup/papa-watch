# PaPaWatch A 版开机动画资源说明

本资源包基于用户选定的 **方案 A** 制作，项目名称统一为 **PaPaWatch**。动画定位不是普通开机 Logo，而是一段适合 1.75 英寸 466×466 AMOLED 圆形电子怀表的短开机仪式：黑场唤醒、机械外圈显现、幸运金币点亮、彩虹童年记忆扫过，最后定格在 Stanley 的专属金币怀表徽章上。

> 推荐把 **PNG 序列**作为正式上机资源，把 **GIF**仅作为视觉预览。LVGL 的 Animation Image 组件支持用多张图像作为动画帧，并可设置播放时长和重复次数；官方文档说明它与普通 Image Widget 类似，只是源图像不是一张，而是一组动画帧。[1] GIF 在 LVGL 中也可使用，但需要启用 `LV_USE_GIF`，并且 GIF 解码会占用额外 RAM；官方文档给出的内存估算为约 25 kB 加上与色彩格式和图像尺寸相关的帧缓冲开销。[2]

| 项目 | 建议值 | 说明 |
| --- | --- | --- |
| 屏幕画布 | **466×466 px** | 与目标圆形 AMOLED 屏幕一致。 |
| 帧数 | **40 帧** | 两秒启动仪式，足够有节奏但不拖沓。 |
| 推荐帧率 | **20 fps** | 40 帧约 2 秒；嵌入式端也可以降到 15 fps。 |
| 正式格式 | **PNG 序列** | 便于转换为 LVGL image C array，也便于逐帧压缩与裁剪。 |
| 预览格式 | **GIF** | 方便在电脑或手机上快速查看动画效果，不建议作为最终嵌入格式。 |
| 最终停留帧 | `boot_papawatch_A_final_hold_466x466.png` | 如果开机动画结束后需要停留，可直接显示这张图。 |

## 文件结构

资源包中的核心目录是 `frames_png/`，其中包含 `boot_papawatch_A_000.png` 到 `boot_papawatch_A_039.png` 共 40 张 PNG 帧。GIF 预览文件为 `boot_papawatch_A_preview.gif`，关键帧总览为 `boot_papawatch_A_contact_sheet.png`，最终停留帧为 `boot_papawatch_A_final_hold_466x466.png`。

正式嵌入时，建议先从 PNG 序列转换为 LVGL 能直接引用的图像数组。若使用 LVGL v9 的 `lv_animimg`，逻辑上可以将各帧声明为 `lv_image_dsc_t`，再把这些帧组成数组传给动画图像对象。官方示例中使用 `lv_animimg_set_src(animimg, dsc, num)` 设置帧数组，并通过 `lv_animimg_set_duration()` 设置动画总时长。[1]

## LVGL 集成思路

以下代码是结构示意，具体变量名需要以你的图片转换工具输出为准。建议在启动屏幕阶段创建一个全屏黑色父容器，把动画对象居中显示。动画播放完成后，可以删除动画对象，切到主菜单；如果希望仪式感更强，也可以先停留最终帧 300–500 ms 再进入主界面。

```c
LV_IMAGE_DECLARE(boot_papawatch_A_000);
LV_IMAGE_DECLARE(boot_papawatch_A_001);
/* ... */
LV_IMAGE_DECLARE(boot_papawatch_A_039);

static const lv_image_dsc_t * boot_frames[] = {
    &boot_papawatch_A_000,
    &boot_papawatch_A_001,
    /* ... */
    &boot_papawatch_A_039,
};

void papawatch_boot_show(void)
{
    lv_obj_t * anim = lv_animimg_create(lv_screen_active());
    lv_obj_center(anim);
    lv_animimg_set_src(anim, (const void **)boot_frames, 40);
    lv_animimg_set_duration(anim, 2000);
    lv_animimg_set_repeat_count(anim, 1);
    lv_animimg_start(anim);
}
```

如果你的固件资源空间比较紧张，可以保留 40 帧版本作为母版，再制作 24 帧或 30 帧轻量版。视觉上最重要的帧段是金币点亮、彩虹扫光和最终定格；前面的黑场渐显可以适度减少帧数。若主控解码能力有限，优先降低帧数，其次再考虑缩小色深或减少透明通道。

## 文案与最终画面

最终定格采用 **PaPaWatch** 作为项目名，金币内环保留 **STANLEY**，底部铭文为 **“For Stanley, my little legend.”**。这句话比直白的 “Papa loves you, forever” 更像一枚定制幸运金币上的刻字，既有父亲的骄傲，也不会显得过于普通。若实机显示时底部小字不够清楚，可以把金币铭文改成更短的 **“Lucky coin. Brave heart.”**。

## References

[1]: https://docs.lvgl.io/9.5/widgets/animimg.html "LVGL 9.5 Documentation: Animation Image (lv_animimg)"
[2]: https://docs.lvgl.io/9.5/widgets/gif.html "LVGL 9.5 Documentation: GIF (lv_gif)"
