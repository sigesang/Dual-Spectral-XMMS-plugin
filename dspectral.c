/*
  Dual Spectralyzer 1.2.1
 -----------------------
  dual spectrum analyzer plugin for XMMS

  by Joakim 'basemetal' Elofsson
*/

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmms/plugin.h>
#include <xmms/configfile.h>

#include "bg-def.xpm"
#include "dspectral_mini.xpm"

#define THIS_IS "Dual Spectralyzer 1.2.1"

#define NUM_BANDS 128

#define CONFIG_SECTION "Dual Spectralizer"

/* THEMEDIR set at maketime */
#define THEME_DEFAULT_STR ""
#define THEME_DEFAULT_PATH THEMEDIR

/*  */
#define FSEL_ALWAYS_DEFAULT_PATH 
/* analyzer */
#define AWIDTH NUM_BANDS
#define AHEIGHT 48
/* window */
#define TOP_BORDER 14
#define BOTTOM_BORDER 6
#define SIDE_BORDER 7
#define WINWIDTH 275
#define WINHEIGHT AHEIGHT+TOP_BORDER+BOTTOM_BORDER

#define TYPE_AVG_NONE 0
#define TYPE_AVG_STEPUPDECAY 1
#define TYPE_AVG_EXP 2 

/* used for nonlinj freq axis */
/* exp(($i+1)/128*log(256))-1  for using the 256point data on 128points */
static gint16 xscl[129]={ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3,
    3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 6, 6, 6, 6,
    7, 7, 8, 8, 8, 9, 9, 10, 10, 11, 11, 12, 13, 13, 14, 14,
    15, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 29, 31,
    32, 33, 35, 37, 38, 40, 42, 44, 46, 48, 50, 52, 55, 57, 60, 62,
    65, 68, 71, 75, 78, 81, 85, 89, 93, 97, 102, 106, 111, 116, 121, 126,
    132, 138, 144, 151, 157, 164, 172, 180, 188, 196, 205, 214, 223, 233, 244, 254,
    256}; /* last one to be sure */

extern GtkWidget *mainwin; /* xmms mainwin */
extern GList *dock_window_list; /* xmms dockwinlist*/

static GtkWidget *window = NULL;
static GtkWidget *drwarea;
static GtkWidget *win_about = NULL;
static GtkWidget *win_conf = NULL;
static GtkWidget *rdbtn_step, *rdbtn_exp;
static GtkWidget *lbl_dbrange, *lbl_scale;
static GtkWidget *hscale_ampscale, *hscale_dbrange;
static GtkWidget *fsel;
static GtkWidget *etry_theme;
static GtkWidget *btn_snapmainwin;

static GdkBitmap *mask=NULL;

static GdkPixmap *bg_pixmap = NULL;
static GdkPixmap *pixmap = NULL;
static GdkGC *gc = NULL;

gfloat  *fdata[2];  /* current temp */
gfloat  *hfdata[2]; /* history */

/* configvars */
typedef struct {
  gfloat   avg_factor;
  gint     avg_mode;
  gboolean amp_gain; 
  gboolean amp_db;
  gboolean freq_nonlinj;
  int      db_scale_factor;
  float    amp_scale;
  char    *skin_xpm;
  gint     pos_x; /* if -1 then ignore */
  gint     pos_y;
  gboolean relmain;
} DSpecCfg;

static DSpecCfg Cfg = {0.75, TYPE_AVG_NONE, TRUE, TRUE, FALSE, 7, 3.0, NULL, -1, -1, 0};

extern GList *dock_add_window(GList *, GtkWidget *);
extern gboolean dock_is_moving(GtkWidget *);
extern void dock_move_motion(GtkWidget *,GdkEventMotion *);
extern void dock_move_press(GList *, GtkWidget *, GdkEventButton *, gboolean);
extern void dock_move_release(GtkWidget *);

static void dspec_about();
static void dspec_config();
static void dspec_init(void);
static void dspec_cleanup(void);
static void dspec_render_freq(gint16 data[2][256]);
static void dspec_config_read();
static GtkWidget* dspec_create_menu(void);
static void create_fileselection (void);

VisPlugin dspec_vp = {
	NULL, NULL, 0,
	THIS_IS,
	0, /* pcm channels */
	2, /* freq channels */
	dspec_init, 
	dspec_cleanup,
	dspec_about,
	dspec_config,
	NULL,
	NULL,
	NULL,
	NULL, 
	dspec_render_freq /* render_freq */
};

VisPlugin *get_vplugin_info (void) {
  return &dspec_vp;
}

static void dspec_destroy_cb (GtkWidget *w,gpointer data) {
  dspec_vp.disable_plugin(&dspec_vp);
}

static void dspec_set_theme() {
  GdkColor color;
  GdkGC *gc2 = NULL;

  if ((Cfg.skin_xpm != NULL) && (strcmp(Cfg.skin_xpm, THEME_DEFAULT_STR) != 0)) {
    bg_pixmap = gdk_pixmap_create_from_xpm(window->window, &mask, NULL, Cfg.skin_xpm);
  }
  if (bg_pixmap == NULL) {
    bg_pixmap = gdk_pixmap_create_from_xpm_d(window->window, &mask, NULL, bg_def_xpm);  
  }
  gdk_window_clear(window->window);

  gc2 = gdk_gc_new(mask); // mask-gc
  color.pixel = 1;
  gdk_gc_set_foreground(gc2, &color);
  gdk_draw_rectangle(mask, gc2, TRUE, SIDE_BORDER, TOP_BORDER, AWIDTH, AHEIGHT);
  gdk_draw_rectangle(mask, gc2, TRUE, WINWIDTH-SIDE_BORDER-AWIDTH, TOP_BORDER, AWIDTH, AHEIGHT);
  color.pixel = 0;
  gdk_gc_set_foreground(gc2, &color);
  gdk_draw_line(mask, gc2, WINWIDTH, 0 ,WINWIDTH , WINHEIGHT-1);
  gtk_widget_shape_combine_mask(window, mask, 0, 0);
  gdk_gc_destroy(gc2);
  
  gdk_draw_pixmap(pixmap, gc, bg_pixmap,
		  0, 0, 0, 0, WINWIDTH, WINHEIGHT);

  gdk_window_clear(drwarea->window);

}

static gint dspec_mousebtnrel_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  if (event->type == GDK_BUTTON_RELEASE) {
    if (dock_is_moving(window)) {
      dock_move_release(window);
    }
    if (event->button == 1) {
      if ((event->x > (WINWIDTH - TOP_BORDER)) &&
	  (event->y < TOP_BORDER)) { //topright corner
	dspec_vp.disable_plugin(&dspec_vp);
      }
    }
  }
  
  return TRUE;
}

static gint dspec_mousemove_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  if (dock_is_moving(window)) {
    dock_move_motion(window, event);
  }

  return TRUE;
}

static gint dspec_mousebtn_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  if (event->type == GDK_BUTTON_PRESS) {
    if ((event->button == 1) &&
	(event->x < (WINWIDTH - TOP_BORDER)) &&
	(event->y <= TOP_BORDER)) { //topright corner
      dock_move_press(dock_window_list, window, event, FALSE);
    }
    
    if (event->button == 3) {
      gtk_menu_popup ((GtkMenu *)data, NULL, NULL, NULL, NULL, 
                            event->button, event->time);
    }
  }

  return TRUE;
}

static void dspec_set_icon (GtkWidget *win)
{
  static GdkPixmap *icon;
  static GdkBitmap *mask;
  Atom icon_atom;
  glong data[2];
  
  if (!icon) {
    icon = gdk_pixmap_create_from_xpm_d (win->window, &mask, 
					 &win->style->bg[GTK_STATE_NORMAL], 
					 dspectral_mini_xpm);
  }
  data[0] = GDK_WINDOW_XWINDOW(icon);
  data[1] = GDK_WINDOW_XWINDOW(mask);
  
  icon_atom = gdk_atom_intern ("KWM_WIN_ICON", FALSE);
  gdk_property_change (win->window, icon_atom, icon_atom, 32,
		       GDK_PROP_MODE_REPLACE, (guchar *)data, 2);
}

static void dspec_init (void) {
  GdkColor color;
  GtkWidget *menu;

  if (window) return;
  
  dspec_config_read();

  fdata[0] = (gfloat *) calloc(NUM_BANDS, sizeof(gfloat));
  fdata[1] = (gfloat *) calloc(NUM_BANDS, sizeof(gfloat));
  hfdata[0] = (gfloat *) calloc(NUM_BANDS, sizeof(gfloat));
  hfdata[1] = (gfloat *) calloc(NUM_BANDS, sizeof(gfloat));
  if (!fdata[0] || !fdata[1] || !hfdata[0] || !hfdata[1]) {
    return;
  }
  
  window = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_widget_set_app_paintable(window, TRUE);
  gtk_window_set_title(GTK_WINDOW(window), THIS_IS);
  gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
  gtk_window_set_wmclass(GTK_WINDOW(window), 
			 "XMMS_Player", "DualSpectralizer");
  gtk_widget_set_usize(window, WINWIDTH, WINHEIGHT);
  gtk_widget_set_events(window, GDK_BUTTON_MOTION_MASK | 
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
  gtk_widget_realize(window);
  dspec_set_icon(window);
  gdk_window_set_decorations(window->window, 0);
  gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_NONE);

  if (Cfg.pos_x != -1) {
    gtk_widget_set_uposition (window, Cfg.pos_x, Cfg.pos_y);
  }

  menu = dspec_create_menu();

  gtk_signal_connect(GTK_OBJECT(window),"destroy",
		     GTK_SIGNAL_FUNC(dspec_destroy_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(window), "destroy",
		     GTK_SIGNAL_FUNC(gtk_widget_destroyed), &window);
  gtk_signal_connect(GTK_OBJECT(window), "button_press_event",
		     GTK_SIGNAL_FUNC(dspec_mousebtn_cb), (gpointer) menu);
  gtk_signal_connect(GTK_OBJECT(window), "button_release_event",
		     GTK_SIGNAL_FUNC(dspec_mousebtnrel_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(window), "motion_notify_event",
		     GTK_SIGNAL_FUNC(dspec_mousemove_cb), NULL);

  gc = gdk_gc_new(window->window);
  gdk_color_black(gdk_colormap_get_system(),&color);
  gdk_gc_set_foreground(gc, &color);

  pixmap = gdk_pixmap_new(window->window, WINWIDTH, WINHEIGHT,
				 gdk_visual_get_best_depth());

  drwarea = gtk_drawing_area_new();
  gtk_widget_show (drwarea);
  gtk_container_add (GTK_CONTAINER (window), drwarea);
  gtk_drawing_area_size((GtkDrawingArea *) drwarea, WINWIDTH, WINHEIGHT);
  gdk_window_set_back_pixmap(drwarea->window, pixmap, 0);
  gdk_window_clear(drwarea->window);
  gtk_widget_realize(drwarea);

  dspec_set_theme();

  gtk_widget_show(window);

  if (!g_list_find(dock_window_list, window)) {
    dock_add_window(dock_window_list, window);
  }
}

static void dspec_config_read () {
  ConfigFile *cfg;
  gchar *filename, *themefile = NULL;

  filename = g_strconcat(g_get_home_dir(), "/.xmms/config", NULL);
  if ((cfg = xmms_cfg_open_file(filename)) != NULL) {
    xmms_cfg_read_int(cfg, CONFIG_SECTION, "avg_mode", &Cfg.avg_mode);
    xmms_cfg_read_float(cfg, CONFIG_SECTION, "avg_factor", &Cfg.avg_factor);
    xmms_cfg_read_boolean(cfg, CONFIG_SECTION, "amp_db", &Cfg.amp_db);
    xmms_cfg_read_boolean(cfg, CONFIG_SECTION, "amp_gain", &Cfg.amp_gain);
    xmms_cfg_read_int(cfg, CONFIG_SECTION, 
		      "db_scale_factor", &Cfg.db_scale_factor);
    xmms_cfg_read_float(cfg, CONFIG_SECTION, 
		      "amp_scale", &Cfg.amp_scale);
    xmms_cfg_read_boolean(cfg, CONFIG_SECTION, "freq_nonlinj", &Cfg.freq_nonlinj);
    xmms_cfg_read_string(cfg, CONFIG_SECTION, "skin_xpm", &themefile);
    if (themefile)
      Cfg.skin_xpm = g_strdup(themefile);

    xmms_cfg_read_int(cfg, CONFIG_SECTION, "pos_x", &Cfg.pos_x);
    xmms_cfg_read_int(cfg, CONFIG_SECTION, "pos_y", &Cfg.pos_y);
    xmms_cfg_free(cfg);
  }
  g_free(filename);
}

static void dspec_config_write () {
  ConfigFile *cfg;
  gchar *filename;
  if (!Cfg.relmain && Cfg.pos_x != -1 && window != NULL)
    gdk_window_get_position(window->window, &Cfg.pos_x, &Cfg.pos_y);
 
  filename = g_strconcat(g_get_home_dir(), "/.xmms/config", NULL);
  if ((cfg = xmms_cfg_open_file(filename)) != NULL) {
    xmms_cfg_write_int(cfg, CONFIG_SECTION, "avg_mode", Cfg.avg_mode);
    xmms_cfg_write_float(cfg, CONFIG_SECTION, "avg_factor", Cfg.avg_factor);
    xmms_cfg_write_boolean(cfg, CONFIG_SECTION, "amp_db", Cfg.amp_db);
    xmms_cfg_write_boolean(cfg, CONFIG_SECTION, "amp_gain", Cfg.amp_gain);
    xmms_cfg_write_int(cfg, CONFIG_SECTION, 
		      "db_scale_factor", Cfg.db_scale_factor);
    xmms_cfg_write_float(cfg, CONFIG_SECTION, 
		      "amp_scale", Cfg.amp_scale);
    xmms_cfg_write_boolean(cfg, CONFIG_SECTION, "freq_nonlinj", Cfg.freq_nonlinj);
    xmms_cfg_write_string(cfg, CONFIG_SECTION, "skin_xpm",
			  (Cfg.skin_xpm != NULL) ? Cfg.skin_xpm : THEME_DEFAULT_STR);
    xmms_cfg_write_int(cfg, CONFIG_SECTION, "pos_x", Cfg.pos_x);
    xmms_cfg_write_int(cfg, CONFIG_SECTION, "pos_y", Cfg.pos_y);
    xmms_cfg_write_file(cfg, filename);
    xmms_cfg_free(cfg);
  }
  g_free(filename);
}

static void dspec_cleanup(void) {
  dspec_config_write();

  if (g_list_find(dock_window_list, window)) {
    g_list_remove(dock_window_list, window);
  }

  if (win_about) gtk_widget_destroy(win_about);
  if (win_conf)  gtk_widget_destroy(win_conf);
  if (window)    gtk_widget_destroy(window);
  if (fsel)      gtk_widget_destroy(fsel);
  if (gc)           { gdk_gc_unref(gc); gc = NULL; }
  if (bg_pixmap)    { gdk_pixmap_unref(bg_pixmap); bg_pixmap = NULL; }
  if (pixmap)    { gdk_pixmap_unref(pixmap); pixmap = NULL; }
  
  if (fdata[0])  free(fdata[0]); 
  if (fdata[1])  free(fdata[1]);
  if (hfdata[0]) free(hfdata[0]);
  if (hfdata[1]) free(hfdata[1]);
  if (Cfg.skin_xpm) g_free(Cfg.skin_xpm);
}

static void dspec_render_freq(gint16 data[2][256]) {
  int i, a, b;
  static gint16 r,l;
  gfloat yr, yl, *pr,*pl;
  gfloat *oldyr,*oldyl;
  gint16 *data_r,*data_l;

  if(!window)
    return;

  /* some messaround with pointers.. for speedup..*/
  oldyl = hfdata[0];
  oldyr = hfdata[1];
  pl = fdata[0];
  pr = fdata[1];
  data_l = data[0];
  data_r = data[1];

  for (i = 0; i < NUM_BANDS; i++) {
    /* convert 256point integerdata to 128point float data (range 0.0-1.0) 
       linjear or nonlinjear */
    if (Cfg.freq_nonlinj) {
      /* nonlinjear*/
      a = xscl[i]; b=1; yr=0.0; yl=0.0;
      do {
	yl += (float) *(data_l+a) / 32678.0;
	yr += (float) *(data_r+a) / 32678.0;
	a++; b++;
      } while( a<xscl[i+1] );
      if (b>1) { yr/=b; yl/=b; }
    }
    else {
      /* linjear */
      yl = (float)( *(data_r+2*i) + *(data_r+2*i+1)) / 65536.0;
      yr = (float)( *(data_l+2*i) + *(data_l+2*i+1)) / 65536.0;
      a = i;
    }
    
    /* calc time avegaring for data if avegaring is on */
    switch (Cfg.avg_mode) {
    case TYPE_AVG_STEPUPDECAY:
      /* step up/decay */
      *oldyr *= Cfg.avg_factor;
      *oldyl *= Cfg.avg_factor;
      if ( *oldyr > yr ) yr= *oldyr;      
      if ( *oldyl > yl ) yl= *oldyl;
      *oldyl = yl-0.000125;
      *oldyr = yr-0.000125;
      break;
    case TYPE_AVG_EXP:
      /* exponential */
      yr = yr * (1-Cfg.avg_factor) + *oldyr * Cfg.avg_factor;
      yl = yl * (1-Cfg.avg_factor) + *oldyl * Cfg.avg_factor;
      *oldyl = yl; *oldyr = yr;
      break;
    default:
      /* no averaging */
      break;
    }
    /* calc 3dbgain for data  */
    if (Cfg.amp_gain) {
      /* really the right thing at all?.. looks good on screen.. but*/
      /* I'm really not sure abaut the correctness of this...*/
      yl *= sqrt((a+1)*22.05/256); // 22.05 is samplefreq/2/1000... is not always right
      yr *= sqrt((a+1)*22.05/256); // but.. very common
    }

    *pl = yl;  *pr = yr;
    oldyl++; oldyr++;
    pl++;    pr++;
  }

  GDK_THREADS_ENTER();

  gdk_draw_pixmap(pixmap, gc, bg_pixmap,
		     SIDE_BORDER, TOP_BORDER,
		     SIDE_BORDER, TOP_BORDER,
		     AWIDTH, AHEIGHT);
  gdk_draw_pixmap(pixmap, gc, bg_pixmap,
		     WINWIDTH - SIDE_BORDER - AWIDTH, TOP_BORDER,
		     WINWIDTH - SIDE_BORDER - AWIDTH, TOP_BORDER,
		     AWIDTH, AHEIGHT);

  pl = fdata[0];
  pr = fdata[1];
	
  for (i = 0; i < NUM_BANDS; i++) {
    /* this ones can produce under/overflow.. but as far as I know thats no harm
        it doesn't crash at least.. */
    /* calc barheight */
    if(Cfg.amp_db) {
      /* dbscale */
      r = (*pr != 0.0) ? AHEIGHT + Cfg.db_scale_factor * log(*pr):0;
      l = (*pl != 0.0) ? AHEIGHT + Cfg.db_scale_factor * log(*pl):0;
      if( r < 0 ) r = 0;
      if( l < 0 ) l = 0;
    } else {
      /* normal */
      r = AHEIGHT * *pr * Cfg.amp_scale;
      l = AHEIGHT * *pl * Cfg.amp_scale;
    }

#ifdef THIS_ONE_IS_UNDEFINED
    gdk_draw_pixmap(pixmap, gc, bg_pixmap,
		    WINWIDTH + 1, AHEIGHT - 1 - l,
		    SIDE_BORDER + i * (AWIDTH / NUM_BANDS), TOP_BORDER + AHEIGHT - l, 
		    (AWIDTH / NUM_BANDS), l);
    gdk_draw_pixmap(pixmap, gc, bg_pixmap,
		    WINWIDTH + 1, AHEIGHT - 1 - r,
 WINWIDTH - SIDE_BORDER - AWIDTH + (NUM_BANDS-1 - i) * (AWIDTH / NUM_BANDS), TOP_BORDER + AHEIGHT - r,
		    (AWIDTH / NUM_BANDS), r);
#else
    gdk_draw_pixmap(pixmap, gc, bg_pixmap, WINWIDTH+1, 0,
		    SIDE_BORDER + i * (AWIDTH / NUM_BANDS), TOP_BORDER + AHEIGHT - l, 
		    (AWIDTH / NUM_BANDS), l);
    gdk_draw_pixmap(pixmap, gc, bg_pixmap, WINWIDTH+1, 0,
		    WINWIDTH - SIDE_BORDER - AWIDTH + (NUM_BANDS-1 - i) * (AWIDTH / NUM_BANDS), TOP_BORDER + AHEIGHT - r,
		    (AWIDTH / NUM_BANDS), r);
#endif
    pl++; pr++;
  }
  gdk_window_clear(drwarea->window);
  GDK_THREADS_LEAVE();
  return;			
}

/* ************************* */
/* aboutwindow callbacks     */
static void on_btn_about_close_clicked (GtkButton *button, gpointer user_data) {
  gtk_widget_destroy(win_about);
  win_about=NULL;
}

/* ************************* */
/* configwindow callbacks    */
static void on_rdbtn_avgtype_toggled(GtkToggleButton *togglebutton, gpointer user_data) {
  Cfg.avg_mode = (int)user_data;
}

static void on_etry_avgfactor_changed(GtkEditable *editable, gpointer user_data) {
  char *txt, buf[100];
  gint err=0;  
  txt=gtk_entry_get_text((GtkEntry *) editable);
  if(txt!=NULL)
    Cfg.avg_factor=atof(txt);
  if(Cfg.avg_factor>=1.0) {Cfg.avg_factor=0.9999; err=1; }
  if(Cfg.avg_factor<0) {Cfg.avg_factor=0.0; err=1; }
  if(err) {
    sprintf(buf, "%f", Cfg.avg_factor);
    gtk_entry_set_text((GtkEntry *) editable, buf);
  }
}

static void on_ckbtn_db_toggled (GtkToggleButton *togglebutton, gpointer user_data) {
  Cfg.amp_db=gtk_toggle_button_get_active(togglebutton);
  gtk_widget_set_sensitive(hscale_dbrange, Cfg.amp_db);
  gtk_widget_set_sensitive(hscale_ampscale, !Cfg.amp_db);
}

static void on_ckbtn_gain_toggled (GtkToggleButton *togglebutton, gpointer user_data) {
  Cfg.amp_gain=gtk_toggle_button_get_active(togglebutton);
}

static void on_rdbtn_freqscl_toggled (GtkToggleButton *togglebutton, gpointer user_data) {
  Cfg.freq_nonlinj=(int) user_data;
}

static void on_btn_snapmainwin_clicked (GtkButton *button, gpointer user_data) {
  gint x, y, w, h;
  if (mainwin != NULL) {
    gdk_window_get_position(mainwin->window, &x, &y);
    gdk_window_get_size(mainwin->window, &w, &h);
    if (window) {
      gdk_window_move(window->window, x, y+h);
    }
    if (gtk_toggle_button_get_active((GtkToggleButton *) user_data)) {
      Cfg.pos_x=x;
      Cfg.pos_y=y+h;
    }
  }
}

static void on_ckbtn_rcoords_toggled (GtkToggleButton *togglebutton, gpointer user_data) {
  if(gtk_toggle_button_get_active(togglebutton)) {
    gdk_window_get_position(window->window, &Cfg.pos_x, &Cfg.pos_y);
  }
  else {
    Cfg.pos_x=-1;
  }
}

static void on_confbtn_close_clicked (GtkButton *button, gpointer user_data) {
  dspec_config_write();
  gtk_widget_destroy(win_conf);
  win_conf=NULL;
}

static void on_adj_ampscale_value_changed (GtkAdjustment *adjustment, 
					   gpointer user_data) {
  char txt[100];
  Cfg.amp_scale=adjustment->value;
  sprintf(txt, "Scale ampiltude with %4.1f", Cfg.amp_scale);
  gtk_label_set_text((GtkLabel *)lbl_scale, txt);
}

static void on_adj_dbrange_value_changed (GtkAdjustment *adjustment, 
					  gpointer user_data) {
  char txt[100];
  Cfg.db_scale_factor=adjustment->value;
  sprintf(txt, "Range is %4.2f db", 48.0*20.0/(float)Cfg.db_scale_factor);
  gtk_label_set_text((GtkLabel *)lbl_dbrange, txt);
}

static void on_etry_theme_changed (GtkEditable *editable, gpointer user_data) {
  g_free(Cfg.skin_xpm);
  Cfg.skin_xpm = g_strdup(gtk_entry_get_text((GtkEntry *) editable));
  if (window) dspec_set_theme();
}

/* ************************* */
/* fileselect callbacks     */
static void on_btn_theme_clicked (GtkButton *button, gpointer user_data) {
  if (fsel == NULL) {
    create_fileselection();
  }
  gtk_widget_show(fsel);
}

static void on_btn_fsel_cancel_clicked (GtkButton *button, gpointer user_data) {
  gtk_widget_destroy(fsel);
  fsel = NULL;
}

static void on_btn_fsel_ok_clicked (GtkButton *button, gpointer user_data) {
  gchar *fname;
  fname=gtk_file_selection_get_filename((GtkFileSelection *) fsel);
  gtk_entry_set_text((GtkEntry *) etry_theme, fname);
  gtk_widget_destroy(fsel);
  fsel = NULL;
}


/* ****                                            */
/* creates aboutwindow if not present and shows it */

#define ABOUT_MARGIN 10
#define ABOUT_WIDTH 300
#define ABOUT_HEIGHT 150

static void dspec_about(void) {
  GtkWidget *vb_main;
  GtkWidget *frm;
  GtkWidget *lbl_author;
  GtkWidget *btn_about_close;

  if (win_about) return;

  win_about = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_widget_realize(win_about);
  gtk_window_set_title (GTK_WINDOW (win_about), "About");
  gtk_signal_connect(GTK_OBJECT(win_about), "destroy", 
		     GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		     &win_about);

  vb_main = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (win_about), vb_main);
  gtk_widget_show (vb_main);

  frm = gtk_frame_new(THIS_IS);
  gtk_box_pack_start (GTK_BOX (vb_main), frm, TRUE, TRUE, 0);
  gtk_widget_set_usize (frm, ABOUT_WIDTH - ABOUT_WIDTH * 2, ABOUT_HEIGHT - ABOUT_MARGIN * 2);
  gtk_container_set_border_width (GTK_CONTAINER (frm), ABOUT_MARGIN);
  gtk_widget_show (frm);

  lbl_author = gtk_label_new ("plugin for XMMS\n"
			      "made by Joakim Elofsson\n"
			      "joakim.elofsson@home.se\n"
			      "   http://www.shell.linux.se/bm/   ");
  gtk_container_add (GTK_CONTAINER (frm), lbl_author);
  gtk_widget_show (lbl_author);

  btn_about_close = gtk_button_new_with_label ("Close");
  gtk_box_pack_start (GTK_BOX (vb_main), btn_about_close, FALSE, FALSE, 0);
  gtk_widget_show (btn_about_close);

  gtk_signal_connect (GTK_OBJECT (btn_about_close), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_about_close_clicked),
                      GTK_OBJECT(win_about));

  gtk_widget_show (win_about);
}

/* ****                                             */
/* creates configwindow if not present and shows it */
static void dspec_config (void) {
  char txt[100];
  GtkWidget *vb_main;
  GtkWidget *nb_main;
  GtkWidget *vb_avg;
  GtkWidget *frm_avgtype;
  GtkWidget *vb_avgtype;
  GSList *vb_avgtype_group = NULL;
  GtkWidget *rdbtn_non;
  GtkWidget *frm_avgconf;
  GtkWidget *hbox;
  GtkWidget *lbl_avgfactor;
  GtkWidget *etry_avgfactor;
  GtkWidget *lbl_avg;
  GtkWidget *frm_freq;
  GtkWidget *vb_freqaxis;
  GSList *vb_freqaxis_group = NULL;
  GtkWidget *rdbtn_linj;
  GtkWidget *rdbtn_nonlinj;
  GtkWidget *lbl_freq;
  GtkWidget *frm_amp;
  GtkWidget *fixed;
  GtkWidget *ckbtn_db;
  GtkWidget *ckbtn_gain;
  GtkWidget *lbl_amp;
  GtkWidget *btn_close;
  GtkWidget *vb_misc;
  GtkWidget *frm_misc;
  GtkWidget *vb_miscwin;

  GtkWidget *ckbtn_rcoords;
  GtkWidget *frm_theme;
  GtkWidget *hb_theme;

  GtkWidget *btn_theme;
  GtkWidget *lbl_misc;

  GtkObject *adj_ampscale;
  GtkObject *adj_dbrange;

  if (win_conf) return;

  if (Cfg.skin_xpm == NULL) { /* if config never read */
    dspec_config_read ();  
  }

  win_conf = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_window_set_title (GTK_WINDOW (win_conf), "Config - " THIS_IS);
  gtk_signal_connect(GTK_OBJECT (win_conf), "destroy", 
		     GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		     &win_conf);

  vb_main = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_main);
  gtk_container_add (GTK_CONTAINER (win_conf), vb_main);

  nb_main = gtk_notebook_new ();
  gtk_widget_show (nb_main);
  gtk_box_pack_start (GTK_BOX (vb_main), nb_main, TRUE, TRUE, 0);

  vb_avg = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_avg);
  gtk_container_add (GTK_CONTAINER (nb_main), vb_avg);

  frm_avgtype = gtk_frame_new ("Avegaring type");
  gtk_widget_show (frm_avgtype);
  gtk_box_pack_start (GTK_BOX (vb_avg), frm_avgtype, FALSE, FALSE, 0);

  vb_avgtype = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_avgtype);
  gtk_container_add (GTK_CONTAINER (frm_avgtype), vb_avgtype);

  rdbtn_non = gtk_radio_button_new_with_label (vb_avgtype_group, "non");
  vb_avgtype_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_non));
  gtk_widget_show (rdbtn_non);
  gtk_box_pack_start (GTK_BOX (vb_avgtype), rdbtn_non, FALSE, FALSE, 0);

  rdbtn_step = gtk_radio_button_new_with_label (vb_avgtype_group, "Step up/decay");
  vb_avgtype_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_step));
  gtk_widget_show (rdbtn_step);
  gtk_box_pack_start (GTK_BOX (vb_avgtype), rdbtn_step, FALSE, FALSE, 0);

  rdbtn_exp = gtk_radio_button_new_with_label (vb_avgtype_group, "Exponential");
  vb_avgtype_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_exp));
  gtk_widget_show (rdbtn_exp);
  gtk_box_pack_start (GTK_BOX (vb_avgtype), rdbtn_exp, FALSE, FALSE, 0);

  frm_avgconf = gtk_frame_new ("Avegaring parameteters");
  gtk_widget_show (frm_avgconf);
  gtk_box_pack_start (GTK_BOX (vb_avg), frm_avgconf, FALSE, FALSE, 0);

  if ( Cfg.avg_mode == TYPE_AVG_EXP ) {
    gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_exp, TRUE);
  }
  else if (Cfg.avg_mode == TYPE_AVG_STEPUPDECAY) {
    gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_step, TRUE);
  }
  else {
    gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_non, TRUE);
  }

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (hbox);
  gtk_container_add (GTK_CONTAINER (frm_avgconf), hbox);

  lbl_avgfactor = gtk_label_new ("Avegaring factor (0.0 - 1.0)");
  gtk_widget_show (lbl_avgfactor);
  gtk_box_pack_start (GTK_BOX (hbox), lbl_avgfactor, FALSE, FALSE, 2);

  etry_avgfactor = gtk_entry_new ();
  gtk_widget_show (etry_avgfactor);
  gtk_box_pack_start (GTK_BOX (hbox), etry_avgfactor, TRUE, TRUE, 0);
  sprintf(txt, "%f", Cfg.avg_factor);
  gtk_entry_set_text((GtkEntry *) etry_avgfactor, txt);

  lbl_avg = gtk_label_new ("Avegaring");
  gtk_widget_show (lbl_avg);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb_main), gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb_main), 0), lbl_avg);

  frm_freq = gtk_frame_new ("Frequency axis");
  gtk_widget_show (frm_freq);
  gtk_container_add (GTK_CONTAINER (nb_main), frm_freq);

  vb_freqaxis = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_freqaxis);
  gtk_container_add (GTK_CONTAINER (frm_freq), vb_freqaxis);

  rdbtn_linj = gtk_radio_button_new_with_label (vb_freqaxis_group, "linjear frequency axis");
  vb_freqaxis_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_linj));
  gtk_widget_show (rdbtn_linj);
  gtk_box_pack_start (GTK_BOX (vb_freqaxis), rdbtn_linj, FALSE, FALSE, 0);

  rdbtn_nonlinj = gtk_radio_button_new_with_label (vb_freqaxis_group, "nonlinjear frequency axis");
  vb_freqaxis_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_nonlinj));
  gtk_widget_show (rdbtn_nonlinj);
  gtk_box_pack_start (GTK_BOX (vb_freqaxis), rdbtn_nonlinj, FALSE, FALSE, 0);
  gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_nonlinj, Cfg.freq_nonlinj);

  lbl_freq = gtk_label_new ("Frequency");
  gtk_widget_show (lbl_freq);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb_main), gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb_main), 1), lbl_freq);

  frm_amp = gtk_frame_new ("Amplitude axis");
  gtk_widget_show (frm_amp);
  gtk_container_add (GTK_CONTAINER (nb_main), frm_amp);

  fixed = gtk_fixed_new ();
  gtk_widget_show (fixed);
  gtk_container_add (GTK_CONTAINER (frm_amp), fixed);

  ckbtn_db = gtk_check_button_new_with_label ("logaritmic(db) ");
  gtk_toggle_button_set_active((GtkToggleButton *) ckbtn_db, Cfg.amp_db);
  gtk_widget_show (ckbtn_db);  
  gtk_fixed_put (GTK_FIXED (fixed), ckbtn_db, 0, 0);
  gtk_widget_set_usize (ckbtn_db, 96, 16);

  ckbtn_gain = gtk_check_button_new_with_label ("gain 3db per octave");
  gtk_toggle_button_set_active((GtkToggleButton *) ckbtn_gain, Cfg.amp_gain);
  gtk_widget_show (ckbtn_gain);  
  gtk_fixed_put (GTK_FIXED (fixed), ckbtn_gain, 0, 80);
  gtk_widget_set_usize (ckbtn_gain, 152, 16);

  sprintf(txt, "Range is %4.2f db", 48.0*20.0 / (float)Cfg.db_scale_factor);
  lbl_dbrange = gtk_label_new (txt);
  gtk_widget_show (lbl_dbrange);
  gtk_fixed_put (GTK_FIXED (fixed), lbl_dbrange, 96, 0);
  gtk_widget_set_usize (lbl_dbrange, 160, 16);
  gtk_misc_set_alignment (GTK_MISC (lbl_dbrange), 0.0, 0.5);

  sprintf(txt, "Scale ampiltude with %4.1f", Cfg.amp_scale);
  lbl_scale = gtk_label_new (txt);
  gtk_widget_show (lbl_scale);
  gtk_fixed_put (GTK_FIXED (fixed), lbl_scale, 96, 40);
  gtk_widget_set_usize (lbl_scale, 160, 16);
  gtk_misc_set_alignment (GTK_MISC (lbl_scale), 0.0, 0.5);

  adj_ampscale = gtk_adjustment_new (Cfg.amp_scale, 0.5, 6, 0, 0, 0);
  hscale_ampscale = gtk_hscale_new (GTK_ADJUSTMENT (adj_ampscale));
  gtk_widget_show (hscale_ampscale);
  gtk_fixed_put (GTK_FIXED (fixed), hscale_ampscale, 96, 56);
  gtk_widget_set_usize (hscale_ampscale, 160, 16);
  gtk_scale_set_draw_value (GTK_SCALE (hscale_ampscale), FALSE);
  gtk_widget_set_sensitive(hscale_ampscale, !Cfg.amp_db);

  adj_dbrange = gtk_adjustment_new (Cfg.db_scale_factor, 4, 20, 1, 0, 0);
  hscale_dbrange = gtk_hscale_new (GTK_ADJUSTMENT (adj_dbrange));
  gtk_widget_show (hscale_dbrange);
  gtk_fixed_put (GTK_FIXED (fixed), hscale_dbrange, 96, 16);
  gtk_widget_set_usize (hscale_dbrange, 160, 16);
  gtk_scale_set_draw_value (GTK_SCALE (hscale_dbrange), FALSE);
  gtk_scale_set_digits (GTK_SCALE (hscale_dbrange), 0);
  gtk_widget_set_sensitive(hscale_dbrange, Cfg.amp_db);

  lbl_amp = gtk_label_new ("Amplitude");
  gtk_widget_show (lbl_amp);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb_main),
			      gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb_main), 2), lbl_amp);

  vb_misc = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_misc);
  gtk_container_add (GTK_CONTAINER (nb_main), vb_misc);

  frm_misc = gtk_frame_new ("Window");
  gtk_widget_show (frm_misc);
  gtk_box_pack_start (GTK_BOX (vb_misc), frm_misc, FALSE, FALSE, 0);

  vb_miscwin = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_miscwin);
  gtk_container_add (GTK_CONTAINER (frm_misc), vb_miscwin);

  btn_snapmainwin = gtk_button_new_with_label ("Snap below mainwindow");
  gtk_widget_show (btn_snapmainwin);
  gtk_box_pack_start (GTK_BOX (vb_miscwin), btn_snapmainwin, FALSE, FALSE, 0);

  ckbtn_rcoords = gtk_check_button_new_with_label ("Remember possision");
  gtk_toggle_button_set_active((GtkToggleButton *) ckbtn_rcoords, (Cfg.pos_x!=-1)?TRUE:FALSE);
  gtk_widget_show (ckbtn_rcoords);
  gtk_box_pack_start (GTK_BOX (vb_miscwin), ckbtn_rcoords, FALSE, FALSE, 0);

  frm_theme = gtk_frame_new ("Theme");
  gtk_widget_show (frm_theme);
  gtk_box_pack_start (GTK_BOX (vb_misc), frm_theme, TRUE, TRUE, 0);

  hb_theme = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (hb_theme);
  gtk_container_add (GTK_CONTAINER (frm_theme), hb_theme);

  etry_theme = gtk_entry_new ();
  gtk_widget_show (etry_theme);
  gtk_box_pack_start (GTK_BOX (hb_theme), etry_theme, TRUE, TRUE, 0);
  gtk_entry_set_editable (GTK_ENTRY (etry_theme), TRUE);
  gtk_entry_set_text((GtkEntry *) etry_theme,
		     Cfg.skin_xpm ? Cfg.skin_xpm : THEME_DEFAULT_STR);

  btn_theme = gtk_button_new_with_label ("Choose Theme");
  gtk_widget_show (btn_theme);
  gtk_box_pack_start (GTK_BOX (hb_theme), btn_theme, FALSE, FALSE, 0);

  lbl_misc = gtk_label_new ("Misc");
  gtk_widget_show (lbl_misc);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb_main),
			      gtk_notebook_get_nth_page(GTK_NOTEBOOK (nb_main), 3), lbl_misc);
  btn_close = gtk_button_new_with_label ("Close");
  gtk_widget_show (btn_close);
  gtk_box_pack_start (GTK_BOX (vb_main), btn_close, FALSE, FALSE, 0);

  gtk_signal_connect (GTK_OBJECT (ckbtn_rcoords), "toggled",
                      GTK_SIGNAL_FUNC (on_ckbtn_rcoords_toggled), NULL);
  gtk_signal_connect (GTK_OBJECT (rdbtn_non), "toggled",
                      GTK_SIGNAL_FUNC (on_rdbtn_avgtype_toggled),
                      (gpointer) TYPE_AVG_NONE);
  gtk_signal_connect (GTK_OBJECT (rdbtn_step), "toggled",
                      GTK_SIGNAL_FUNC (on_rdbtn_avgtype_toggled),
                      (gpointer) TYPE_AVG_STEPUPDECAY);
  gtk_signal_connect (GTK_OBJECT (rdbtn_exp), "toggled",
                      GTK_SIGNAL_FUNC (on_rdbtn_avgtype_toggled),
                      (gpointer) TYPE_AVG_EXP);
  gtk_signal_connect (GTK_OBJECT (etry_avgfactor), "changed",
                      GTK_SIGNAL_FUNC (on_etry_avgfactor_changed), NULL);
  gtk_signal_connect (GTK_OBJECT (rdbtn_linj), "toggled",
                      GTK_SIGNAL_FUNC (on_rdbtn_freqscl_toggled),
                      (gpointer) FALSE);
  gtk_signal_connect (GTK_OBJECT (rdbtn_nonlinj), "toggled",
                      GTK_SIGNAL_FUNC (on_rdbtn_freqscl_toggled),
                      (gpointer) TRUE);
  gtk_signal_connect (GTK_OBJECT (ckbtn_db), "toggled",
                      GTK_SIGNAL_FUNC (on_ckbtn_db_toggled), NULL);
  gtk_signal_connect (GTK_OBJECT (btn_snapmainwin), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_snapmainwin_clicked),
                      (gpointer) ckbtn_rcoords);
  gtk_signal_connect (GTK_OBJECT (ckbtn_gain), "toggled",
                      GTK_SIGNAL_FUNC (on_ckbtn_gain_toggled),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (btn_close), "clicked",
                      GTK_SIGNAL_FUNC (on_confbtn_close_clicked),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (btn_theme), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_theme_clicked),
		      NULL);
  gtk_signal_connect (GTK_OBJECT (etry_theme), "changed",
                      GTK_SIGNAL_FUNC (on_etry_theme_changed),
		      NULL);
  gtk_signal_connect (adj_ampscale, "value-changed",
                      GTK_SIGNAL_FUNC (on_adj_ampscale_value_changed),
                      NULL);
  gtk_signal_connect (adj_dbrange, "value-changed",
                      GTK_SIGNAL_FUNC (on_adj_dbrange_value_changed),
                      NULL);

  gtk_widget_show(win_conf);
}

void create_fileselection (void) {
  GtkWidget *btn_fsel_cancel;
  GtkWidget *btn_fsel_ok;
  gchar *themefile = NULL;

  fsel = gtk_file_selection_new ("Välj fil");
  gtk_object_set_data (GTK_OBJECT (fsel), "fsel", fsel);
  gtk_container_set_border_width (GTK_CONTAINER (fsel), 5);

  btn_fsel_ok = GTK_FILE_SELECTION (fsel)->ok_button;
  gtk_object_set_data (GTK_OBJECT (fsel), "btn_fsel_ok", btn_fsel_ok);
  gtk_widget_show (btn_fsel_ok);
  GTK_WIDGET_SET_FLAGS (btn_fsel_ok, GTK_CAN_DEFAULT);

  btn_fsel_cancel = GTK_FILE_SELECTION (fsel)->cancel_button;
  gtk_object_set_data (GTK_OBJECT (fsel), 
		       "btn_fsel_cancel", btn_fsel_cancel);
  gtk_widget_show (btn_fsel_cancel);
  GTK_WIDGET_SET_FLAGS (btn_fsel_cancel, GTK_CAN_DEFAULT);

#ifndef FSEL_ALWAYS_DEFAULT_PATH
  themefile = Cfg.skin_xpm;
  if (!themefile && (strcmp(Cfg.skin_xpm, THEME_DEFAULT_STR) == 0))
    themefile = (THEME_DEFAULT_PATH);
#else
  themefile = (THEME_DEFAULT_PATH);
#endif

  gtk_file_selection_set_filename((GtkFileSelection *) fsel, themefile);

  gtk_signal_connect (GTK_OBJECT (btn_fsel_cancel), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_fsel_cancel_clicked),
		      NULL);
  gtk_signal_connect (GTK_OBJECT (btn_fsel_ok), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_fsel_ok_clicked),
		      NULL);
}

/*GtkWidget *item_follow;*/

void on_item_close_activate(GtkMenuItem *menuitem, gpointer data)
{
  dspec_vp.disable_plugin(&dspec_vp);
}

void on_item_about_activate(GtkMenuItem *menuitem, gpointer data)
{
  dspec_about();
}

void on_item_conf_activate(GtkMenuItem *menuitem, gpointer data)
{
  dspec_config();
}

GtkWidget* dspec_create_menu(void)
{
  GtkWidget *menu;
  GtkAccelGroup *m_acc;
  
  GtkWidget *sep;
  GtkWidget *item_close;
  GtkWidget *item_about;
  GtkWidget *item_conf;

  menu = gtk_menu_new();
  m_acc = gtk_menu_ensure_uline_accel_group(GTK_MENU(menu));

  item_about = gtk_menu_item_new_with_label("About " THIS_IS);
  gtk_widget_show(item_about);
  gtk_container_add (GTK_CONTAINER(menu), item_about);

  sep = gtk_menu_item_new ();
  gtk_widget_show(sep);
  gtk_container_add (GTK_CONTAINER(menu), sep);
  gtk_widget_set_sensitive(sep, FALSE);
/*
  item_follow = gtk_menu_item_new_with_label("Add to XMMS window dock list");
  gtk_widget_show(item_follow);
  gtk_container_add(GTK_CONTAINER(menu), item_follow);
*/
  item_conf = gtk_menu_item_new_with_label("Config");
  gtk_widget_show(item_conf);
  gtk_container_add (GTK_CONTAINER(menu), item_conf);

  item_close = gtk_menu_item_new_with_label("Close");
  gtk_widget_show(item_close);
  gtk_container_add (GTK_CONTAINER(menu), item_close);

  gtk_signal_connect(GTK_OBJECT(item_close), "activate",
		     GTK_SIGNAL_FUNC(on_item_close_activate), NULL);

  gtk_signal_connect(GTK_OBJECT(item_about), "activate",
		     GTK_SIGNAL_FUNC(on_item_about_activate), NULL);

  gtk_signal_connect(GTK_OBJECT(item_conf), "activate",
		     GTK_SIGNAL_FUNC(on_item_conf_activate), NULL);

/*
  gtk_signal_connect(GTK_OBJECT(item_follow), "activate",
		     GTK_SIGNAL_FUNC(on_item_follow_activate), NULL);
*/
  return menu;
}
