/* Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/gst.h>

/* include this header if you want to use dynamic parameters
#include <gst/control/control.h>
*/

#include "bkr_video_out.h"

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SILENT
};

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("ANY")
);

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("ANY")
);

static void	gst_plugin_template_class_init	(GstPluginTemplateClass *klass);
static void	gst_plugin_template_base_init	(GstPluginTemplateClass *klass);
static void	gst_plugin_template_init	(GstPluginTemplate *filter);

static void	gst_plugin_template_set_property(GObject *object, guint prop_id,
                                                 const GValue *value,
					         GParamSpec *pspec);
static void	gst_plugin_template_get_property(GObject *object, guint prop_id,
                                                 GValue *value,
						 GParamSpec *pspec);

static void	gst_plugin_template_update_plugin(const GValue *value,
						  gpointer data);
static void	gst_plugin_template_update_mute	(const GValue *value,
						 gpointer data);

static void	gst_plugin_template_chain	(GstPad *pad, GstData *in);

static GstElementClass *parent_class = NULL;

/* this function handles the link with other plug-ins */
static GstPadLinkReturn
gst_plugin_template_link (GstPad *pad, const GstCaps *caps)
{
  GstPluginTemplate *filter;
  GstPad *otherpad;

  filter = GST_PLUGIN_TEMPLATE (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_PLUGIN_TEMPLATE (filter),
                        GST_PAD_LINK_REFUSED);
  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);

  /* set caps on next or previous element's pad, and see what they
   * think. In real cases, we would (after this step) extract
   * properties from the caps such as video size or audio samplerat. */
  return gst_pad_try_set_caps (otherpad, caps);
}

GType
gst_gst_plugin_template_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type)
  {
    static const GTypeInfo plugin_info =
    {
      sizeof (GstPluginTemplateClass),
      (GBaseInitFunc) gst_plugin_template_base_init,
      NULL,
      (GClassInitFunc) gst_plugin_template_class_init,
      NULL,
      NULL,
      sizeof (GstPluginTemplate),
      0,
      (GInstanceInitFunc) gst_plugin_template_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
	                                  "GstPluginTemplate",
	                                  &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_plugin_template_base_init (GstPluginTemplateClass *klass)
{
  static GstElementDetails plugin_details = {
    "PluginTemplate",
    "Generic/PluginTemplate",
    "Generic Template Plugin",
    "Thomas Vander Stichele <thomas@apestaart.org>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

/* initialize the plugin's class */
static void
gst_plugin_template_class_init (GstPluginTemplateClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_SILENT,
    g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
                          FALSE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_plugin_template_set_property;
  gobject_class->get_property = gst_plugin_template_get_property;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_plugin_template_init (GstPluginTemplate *filter)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (filter);

  filter->sinkpad = gst_pad_new_from_template (
	gst_element_class_get_pad_template (klass, "sink"), "sink");
  gst_pad_set_link_function (filter->sinkpad, gst_plugin_template_link);
  gst_pad_set_getcaps_function (filter->sinkpad, gst_pad_proxy_getcaps);

  filter->srcpad = gst_pad_new_from_template (
	gst_element_class_get_pad_template (klass, "src"), "src");
  gst_pad_set_link_function (filter->srcpad, gst_plugin_template_link);
  gst_pad_set_getcaps_function (filter->srcpad, gst_pad_proxy_getcaps);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_plugin_template_chain);
  filter->silent = FALSE;
}

/* chain function
 * this function does the actual processing
 */

static void
gst_plugin_template_chain (GstPad *pad, GstData *in)
{
  GstPluginTemplate *filter;
  GstBuffer *out_buf, *buf = GST_BUFFER (in);
  gfloat *data;
  gint i, num_samples;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_PLUGIN_TEMPLATE (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_IS_PLUGIN_TEMPLATE (filter));

  if (filter->silent == FALSE)
    g_print ("I'm plugged, therefore I'm in.\n");

  /* just push out the incoming buffer without touching it */
  gst_pad_push (filter->srcpad, GST_DATA (buf));
}

static void
gst_plugin_template_set_property (GObject *object, guint prop_id,
                                  const GValue *value, GParamSpec *pspec)
{
  GstPluginTemplate *filter;

  g_return_if_fail (GST_IS_PLUGIN_TEMPLATE (object));
  filter = GST_PLUGIN_TEMPLATE (object);

  switch (prop_id)
  {
  case ARG_SILENT:
    filter->silent = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_plugin_template_get_property (GObject *object, guint prop_id,
                                  GValue *value, GParamSpec *pspec)
{
  GstPluginTemplate *filter;

  g_return_if_fail (GST_IS_PLUGIN_TEMPLATE (object));
  filter = GST_PLUGIN_TEMPLATE (object);

  switch (prop_id) {
  case ARG_SILENT:
    g_value_set_boolean (value, filter->silent);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "plugin",
			       GST_RANK_NONE,
			       GST_TYPE_PLUGIN_TEMPLATE);
}

/* this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered */
GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "plugin",
  "Template plugin",
  plugin_init,
  VERSION,
  "LGPL",
  "GStreamer",
  "http://gstreamer.net/"
)
