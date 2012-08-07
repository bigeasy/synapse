/* I can't seem to find an API or other known way to store a plugin's
 * configuration. Adobe Flash offers its configuration in a popup bar, but I
 * don't know know where that gets stored. */
#include "vendor/mozilla/npfunctions.h"
#include <dlfcn.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <curl/curl.h>
#include "vendor/attendant/attendant.h"
#include "vendor/attendant/eintr.h"

/* *Tell me now, Muses who have your homes on Olympus &mdash; for you are
 * goddesses, and are present, and know everything, while we hear only rumor and
 * know nothing.* &mdash; Homer */

/* Declare all of the methods provded by the browser. When the plugin is
 * initialized, we will copy the functions from the structure in the browser
 * into these static variables. We favor a lowered-case, underbared naming
 * convention. */
static NPN_GetURLProcPtr npn_get_url;
static NPN_PostURLProcPtr npn_post_url;
static NPN_RequestReadProcPtr npn_request_read;
static NPN_NewStreamProcPtr npn_new_stream;
static NPN_WriteProcPtr npn_write;
static NPN_DestroyStreamProcPtr npn_destroy_stream;
static NPN_StatusProcPtr npn_status;
static NPN_UserAgentProcPtr npn_user_agent;
static NPN_MemAllocProcPtr npn_mem_alloc;
static NPN_MemFreeProcPtr npn_mem_free;
static NPN_MemFlushProcPtr npn_mem_flush;
static NPN_ReloadPluginsProcPtr npn_reload_plugins;
static NPN_GetJavaEnvProcPtr npn_get_java_env;
static NPN_GetJavaPeerProcPtr npn_get_java_peer;
static NPN_GetURLNotifyProcPtr npn_get_url_notify;
static NPN_PostURLNotifyProcPtr npn_post_url_notify;
static NPN_GetValueProcPtr npn_get_value;
static NPN_SetValueProcPtr npn_set_value;
static NPN_InvalidateRectProcPtr npn_invalidate_rect;
static NPN_InvalidateRegionProcPtr npn_invalidate_region;
static NPN_ForceRedrawProcPtr npn_force_redraw;
static NPN_GetStringIdentifierProcPtr npn_get_string_identifier;
static NPN_GetStringIdentifiersProcPtr npn_get_string_identifiers;
static NPN_GetIntIdentifierProcPtr npn_get_int_identifier;
static NPN_IdentifierIsStringProcPtr npn_identifier_is_string;
static NPN_UTF8FromIdentifierProcPtr npn_utf8_from_indentifier;
static NPN_IntFromIdentifierProcPtr npn_int_from_identifier;
static NPN_CreateObjectProcPtr npn_create_object;
static NPN_RetainObjectProcPtr npn_retain_object;
static NPN_ReleaseObjectProcPtr npn_release_object;
static NPN_InvokeProcPtr npn_invoke;
static NPN_InvokeDefaultProcPtr npn_invoke_default;
static NPN_EvaluateProcPtr npn_evaluate;
static NPN_GetPropertyProcPtr npn_get_property;
static NPN_SetPropertyProcPtr npn_set_property;
static NPN_RemovePropertyProcPtr npn_remove_property;
static NPN_HasPropertyProcPtr npn_has_property;
static NPN_HasMethodProcPtr npn_has_method;
static NPN_ReleaseVariantValueProcPtr npn_release_variant_value;
static NPN_SetExceptionProcPtr npn_set_exception;
static NPN_PushPopupsEnabledStateProcPtr npn_push_popups_enabled_state;
static NPN_PopPopupsEnabledStateProcPtr npn_pop_popups_enabled_state;
static NPN_EnumerateProcPtr npn_enumerate;
static NPN_PluginThreadAsyncCallProcPtr npn_plugin_thread_async_call;
static NPN_ConstructProcPtr npn_construct;
static NPN_GetValueForURLPtr npn_get_value_for_url;
static NPN_SetValueForURLPtr npn_set_value_for_url;
static NPN_GetAuthenticationInfoPtr npn_get_authentication_info;
static NPN_ScheduleTimerPtr npn_schedule_timer;
static NPN_UnscheduleTimerPtr npn_unschedule_timer;
static NPN_PopUpContextMenuPtr npn_pop_up_context_menu;
static NPN_ConvertPointPtr npn_convert_point;
static NPN_HandleEventPtr npn_handle_event;
static NPN_UnfocusInstancePtr npn_unfocus_instance;
static NPN_URLRedirectResponsePtr npn_url_redirect_response;

/* Gather the plugin library state into a structure. */

/* &#9824; */
struct plugin {
  /* The file descriptor of the UNIX error log. */
  int log;
  /* The full path to the relay program. */
  char relay[PATH_MAX];
  /* The full path to node. */
  char node[PATH_MAX];
  /* The full path to the node process monitor. */
  char monitor[PATH_MAX];
  /* The shutdown key for the process monitor. */
  char shutdown[1024 * 5];
  /* The local port of the peer. */
  int port;
  /* Used to perform the GET that shuts down the Cubby web server. */
  CURL *curl;
  /* Gaurd process variables referenced by both the stub functions running in
   * the plugin threads and the server process management threads. */
  pthread_mutex_t mutex;      
  /* Server process is running. */
  pthread_cond_t cond;  
  /* Time to wait before restarting. */
  int backoff;
/* &mdash; */
};

/* The one and only instance of the plugin library state. */
static struct plugin plugin;

/* Say something into the debugging log. */
extern void say(const char* format, ...);

/* Entry points for the plugin dynamic link library. Note that, the `main`
 * function that was once necessary for Mac OS no longer applies to OS X
 * browsers. It likes to confuse the compiler, a strange main. */

/* */
#pragma export on

/* Initialize the library with the browser functions, initialize the library. */
NPError NP_Initialize(NPNetscapeFuncs *browser);
/* Obtain the entry point funtions for the plugin. */
NPError NP_GetEntryPoints(NPPluginFuncs *class);
/* Shutdown the plugin library. */
void OSCALL NP_Shutdown();

/* */
#pragma export off

void starter(int restart, int uptime) {
  char const *argv[] = { plugin.monitor, NULL };

  say("starter: restart %d, uptime %d", restart, uptime);

  (void) pthread_mutex_lock(&plugin.mutex);
  plugin.port = 0;
  (void) pthread_mutex_unlock(&plugin.mutex);

  if (uptime > 3600) {
    plugin.backoff = 0;
  }

  attendant.start(plugin.node, argv, plugin.backoff * 1000);
 
  if (plugin.backoff == 0) {
    plugin.backoff = 2;
  } else if (plugin.backoff < 512) {
    plugin.backoff *= 2;
  }
}

void connector(attendant__pipe_t in, attendant__pipe_t out) {
  /* Tolerance: 1024 is not a magic number. It is a kilobyte. When I see this, I
   * know what it means. It means that I'm reading output line by line, and no
   * line will ever be more that 64 characters, so a kilobyte is plenty.
   *
   * What good does it do to give this number a name? (Again, it has a name when
   * I read it; kilobyte.) Does it make it any easier to resolve the problem
   * that some day in the future, a line might be more than 80 characters? If
   * there is a bug introduced, because I've decided that, in the program we
   * launch here, instead of emitting two lines of terse strings, I'm going to
   * dump a megabyte of binary data, will a named number help me find the error
   * any faster? Or will it take longer, because to change the buffer size, I
   * have to search for the place where the name is defined? Or am I in for a
   * penny, in for a pound, and defining these in some distant build step, a
   * configuration file, our using a build tool?
   *
   * Interesting reflection on named numbers versus "magic" numbers, that it
   * made sense to record, here.
   */

  /* */
  char buffer[1024 * 10], *start = buffer, *newline;
  struct pollfd fd;
  int err, offset = 0, newlines = 0;

  fd.fd = out;
  fd.events = POLLIN;

  say("CONNECTING!");

  err = fcntl(out, F_SETFL, O_NONBLOCK);
  say("CONNECTOR! %d", err);

  start = buffer;
  memset(buffer, 0, sizeof(buffer));
  while (newlines != 2) {
    newlines = 0;
    fd.revents = 0;

    say("poll");
    HANDLE_EINTR(poll(&fd, 1, -1), err);

    err = read(out, start + offset, sizeof(buffer) - offset);
    if (err == -1) {
      break;
    }
    offset += err;
    newline = buffer;
    while ((newline = strchr(newline, '\n')) != NULL) {
      newlines++;
      newline++;
    }
  }

  say("done");

  newline = strchr(buffer, '\n');
  strncpy(plugin.shutdown, buffer, newline- buffer);
  plugin.port = atoi(++newline);

  say("SHUTDOWN: %s, PORT: %d", plugin.shutdown, plugin.port);

  (void) pthread_mutex_lock(&plugin.mutex);
  (void) pthread_cond_signal(&plugin.cond);
  (void) pthread_mutex_unlock(&plugin.mutex);

  say("SHUTDOWN: %s, PORT: %d", plugin.shutdown, plugin.port);
}

NPError OSCALL NP_Initialize(NPNetscapeFuncs *browser) {
  const char *home;
  char logfile[PATH_MAX];
  const char* dir;
  Dl_info library;
  struct attendant__initializer initializer;

  say("NP_Initialize");

  /* Copy the browser NPAPI functions into our library. */
  npn_get_url = browser->geturl;
  npn_post_url = browser->posturl;
  npn_request_read = browser->requestread;
  npn_new_stream = browser->newstream;
  npn_write = browser->write;
  npn_destroy_stream = browser->destroystream;
  npn_status = browser->status;
  npn_user_agent = browser->uagent;
  npn_mem_alloc = browser->memalloc;
  npn_mem_free = browser->memfree;
  npn_mem_flush = browser->memflush;
  npn_reload_plugins = browser->reloadplugins;
  npn_get_java_env = browser->getJavaEnv;
  npn_get_java_peer = browser->getJavaPeer;
  npn_get_url_notify = browser->geturlnotify;
  npn_post_url_notify = browser->posturlnotify;
  npn_get_value = browser->getvalue;
  npn_set_value = browser->setvalue;
  npn_invalidate_rect = browser->invalidaterect;
  npn_invalidate_region = browser->invalidateregion;
  npn_force_redraw = browser->forceredraw;
  npn_get_string_identifier = browser->getstringidentifier;
  npn_get_string_identifiers = browser->getstringidentifiers;
  npn_get_int_identifier = browser->getintidentifier;
  npn_identifier_is_string = browser->identifierisstring;
  npn_utf8_from_indentifier = browser->utf8fromidentifier;
  npn_int_from_identifier = browser->intfromidentifier;
  npn_create_object = browser->createobject;
  npn_retain_object = browser->retainobject;
  npn_release_object = browser->releaseobject;
  npn_invoke = browser->invoke;
  npn_invoke_default = browser->invokeDefault;
  npn_evaluate = browser->evaluate;
  npn_get_property = browser->getproperty;
  npn_set_property = browser->setproperty;
  npn_remove_property = browser->removeproperty;
  npn_has_property = browser->hasproperty;
  npn_has_method = browser->hasmethod;
  npn_release_variant_value = browser->releasevariantvalue;
  npn_set_exception = browser->setexception;
  npn_push_popups_enabled_state = browser->pushpopupsenabledstate;
  npn_pop_popups_enabled_state = browser->poppopupsenabledstate;
  npn_enumerate = browser->enumerate;
  npn_plugin_thread_async_call = browser->pluginthreadasynccall;
  npn_construct = browser->construct;
  npn_get_value_for_url = browser->getvalueforurl;
  npn_set_value_for_url = browser->setvalueforurl;
  npn_get_authentication_info = browser->getauthenticationinfo;
  npn_schedule_timer = browser->scheduletimer;
  npn_unschedule_timer = browser->unscheduletimer;
  npn_pop_up_context_menu = browser->popupcontextmenu;
  npn_convert_point = browser->convertpoint;
  npn_handle_event = browser->handleevent;
  npn_unfocus_instance = browser->unfocusinstance;
  npn_url_redirect_response = browser->urlredirectresponse;

  /* Create our mutex and signaling device. */
  (void) pthread_mutex_init(&plugin.mutex, NULL);
  (void) pthread_cond_init(&plugin.cond, NULL);

  dladdr(NP_Initialize, &library);
  dir = library.dli_fname + strlen(library.dli_fname);
  while (*dir != '/') {
    dir--;
  }
  strncat(plugin.node, library.dli_fname, dir - library.dli_fname);
  strcat(plugin.node, "/node");

  memset(&initializer, 0, sizeof(struct attendant__initializer));
  strncat(initializer.relay, library.dli_fname, dir - library.dli_fname);
  strcat(initializer.relay, "/relay");
  strncat(plugin.monitor, library.dli_fname, dir - library.dli_fname);
  strcat(plugin.monitor, "/monitor.js");

  initializer.starter = starter;
  initializer.connector = connector;
  initializer.canary = 31;

  attendant.initialize(&initializer);

  starter(0, -1);

  say("node: %s", plugin.node);
  say("relay: %s", initializer.relay);

  plugin.curl = curl_easy_init();

  return NPERR_NO_ERROR;
}

/* Our plugin has a scriptable object. This is it. */
struct synapse_object {
  NPClass *class;
  uint32_t reference_count;
};

struct NPClass synapse_class;

NPObject* Synapse_Allocate(NPP npp, NPClass *class) {
  struct synapse_object *object;
  say("Synapse_Allocate");
  object = (struct synapse_object*) malloc(sizeof(struct synapse_object));
  object->class = &synapse_class; 
  object->reference_count = 1;
  return (NPObject*) object;
}

void Synapse_Deallocate(NPObject *object) {
  say("Synapse_Dellocate");
}

void Synapse_Invalidate(NPObject *object) {
}

bool Synapse_HasMethod(NPObject *object, NPIdentifier name) {
  return false;
}

bool Synapse_Invoke(NPObject *object, NPIdentifier name, const NPVariant *argv,
    uint32_t argc, NPVariant *result) {
  return true;
}

bool Synapse_InvokeDefault(NPObject *object, const NPVariant *argv,
    uint32_t argc, NPVariant *result) {
  return true;
}

bool Synapse_HasProperty(NPObject *object, NPIdentifier name) {
  bool exists = false;
  if (npn_identifier_is_string(name)) {
    NPUTF8* string = npn_utf8_from_indentifier(name);
    exists = strcmp(string, "port") == 0;
    npn_mem_free(string);
  }
  return exists;
}

bool Synapse_GetProperty(NPObject *object, NPIdentifier name, NPVariant *result) {
  bool exists = false;
  if (npn_identifier_is_string(name)) {
    NPUTF8* string = npn_utf8_from_indentifier(name);
    say("Synapse_GetProperty: %s", string);
    exists = strcmp(string, "port") == 0;
    if (exists) {
      result->type = NPVariantType_Int32;
      (void) pthread_mutex_lock(&plugin.mutex);
      while (plugin.port == 0) {
        pthread_cond_wait(&plugin.cond, &plugin.mutex);
      }
      result->value.intValue = plugin.port;
      (void) pthread_mutex_unlock(&plugin.mutex);
    }
    npn_mem_free(string);
  }
  return exists;
}

bool Synapse_SetProperty(NPObject *object, NPIdentifier name, const NPVariant *value) {
  return false;
}

bool Synapse_RemoveProperty(NPObject *object, NPIdentifier name) {
  return false;
}

bool Synapse_Enumeration(NPObject *object, NPIdentifier **value, uint32_t *count) {
  return false;
}

bool Synapse_Construct(NPObject *object, const NPVariant *argv, uint32_t argc,
    NPVariant *result) {
  return false;
}

struct NPClass synapse_class = {
  NP_CLASS_STRUCT_VERSION, Synapse_Allocate, Synapse_Deallocate,
  Synapse_Invalidate, Synapse_HasMethod, Synapse_Invoke, Synapse_InvokeDefault,
  Synapse_HasProperty, Synapse_GetProperty, Synapse_SetProperty,
  Synapse_RemoveProperty, Synapse_Enumeration, Synapse_Construct
};

NPError NP_LOADDS Synapse_New(NPMIMEType mime, NPP instance, uint16_t mode,
  int16_t argc, char* argn[], char* argv[], NPSavedData* saved) {
  say("NPP_New");
  return NPERR_NO_ERROR;
}

NPError NP_LOADDS Synapse_Destroy(NPP instance, NPSavedData** save) {
  say("NPP_Destroy");
  return NPERR_NO_ERROR;
}

NPError NP_LOADDS Synapse_SetWindow(NPP instance, NPWindow* window) {
  say("NPP_SetWindow");
  return NPERR_NO_ERROR;
}

NPError NP_LOADDS Synapse_NewStream(NPP instance, NPMIMEType type,
    NPStream *stream, NPBool seekable, uint16_t* stype) {
  say("NPP_NewStream");
  return NPERR_NO_ERROR;
}

NPError NP_LOADDS Synapse_DestroyStream(NPP instance, NPStream* stream,
    NPReason reason) {
  say("NPP_DestroyStream");
  return NPERR_NO_ERROR;
}

void NP_LOADDS Synapse_AsFile(NPP instance, NPStream* stream, const char* fname) {
  say("NPP_StreamAsFile");
}

int32_t NP_LOADDS Synapse_WriteReady(NPP instance, NPStream *stream) {
  say("NPP_WriteReady");
  return 0;
}

int32_t NP_LOADDS Synapse_Write(NPP instance, NPStream *stream, int32_t offset,
    int32_t len, void *buffer) {
  say("NPP_Write");
  return 0;
}

void NP_LOADDS Synapse_Print(NPP instance, NPPrint *print) {
  say("NPP_Print");
}

// We do not trace this function because Google Chrome sends a null event every
// tenth of a second or so. We do get an activate event initially, with a window
// handle to the activated window.
//
// We don't handle any of these messages, so we return false. Somewhere, it is
// said that null events should be handled to keep the browser from handling
// them, but I found that too amusing to take seriously.

//
int16_t NP_LOADDS Synapse_HandleEvent(NPP instance, void *event) {
  return 0;
}

void NP_LOADDS Synapse_URLNotify(NPP instance, const char *url,
    NPReason reason, void *data) {
  say("NPP_URLNotify");
}

NPError NP_LOADDS Synapse_GetValue(NPP instance, NPPVariable variable,
    void *value) {
  switch (variable) {
    case NPPVpluginScriptableNPObject:
      (*(NPObject**)value) = npn_create_object(instance, &synapse_class);  
    default:
      break;
  }
  say("NPP_GetValue %d", variable);
  return NPERR_NO_ERROR;
}

NPError NP_LOADDS Synapse_SetValue(NPP instance, NPNVariable variable,
    void *value) {
  say("NPP_SetValue");
  return NPERR_NO_ERROR;
}

NPBool NP_LOADDS Synapse_GotFocus(NPP instance, NPFocusDirection direction) {
  say("NPP_GotFocus");
  return NPERR_NO_ERROR;
}

void NP_LOADDS Synapse_LostFocus(NPP instance) {
  say("NPP_LostFocus");
}

void NP_LOADDS Synapse_URLRedirectNotify(NPP instance, const char *url,
    int32_t status, void *data) {
  say("NPP_URLRedirectNotify");
}

NPError NP_LOADDS Synapse_ClearSiteData(const char *site, uint64_t flags, uint64_t maxAge) {
  say("NPP_ClearSiteData");
  return NPERR_NO_ERROR;
}

char** NP_LOADDS Synapse_GetSitesWithData() {
  say("NPP_GetSitesWithData");
  return NULL;
}

NPError OSCALL NP_GetEntryPoints(NPPluginFuncs *class) {
  say("NP_GetEntryPoints");
  class->size = sizeof(NPPluginFuncs);
  class->version = (NP_VERSION_MAJOR << 8) + NP_VERSION_MINOR;
  /* TODO Lower case these. */
  class->newp = Synapse_New;
  class->destroy = Synapse_Destroy;
  class->setwindow = Synapse_SetWindow;
  class->newstream = Synapse_NewStream;
  class->destroystream = Synapse_DestroyStream;
  class->asfile = Synapse_AsFile;
  class->writeready = Synapse_WriteReady;
  class->write = Synapse_Write;
  class->print = Synapse_Print;
  class->event = Synapse_HandleEvent;
  class->urlnotify = Synapse_URLNotify;
  class->getvalue = Synapse_GetValue;
  class->setvalue = Synapse_SetValue;
  class->gotfocus = Synapse_GotFocus;
  class->lostfocus = Synapse_LostFocus;
  class->urlredirectnotify = Synapse_URLRedirectNotify;
  class->clearsitedata = Synapse_ClearSiteData;
  class->getsiteswithdata = Synapse_GetSitesWithData;
  return NPERR_NO_ERROR;
}

void OSCALL NP_Shutdown() {
  say("NP_Shutdown");
  char url[4096];
  attendant.shutdown();
  if (plugin.curl) {
    sprintf(url, "http://127.0.0.1:%d/shutdown?%s", plugin.port, plugin.shutdown);
    say("Requesting shutdown: %s.", url);
    curl_easy_setopt(plugin.curl, CURLOPT_URL, url);
    curl_easy_perform(plugin.curl);
    curl_easy_cleanup(plugin.curl);
  }
  if (!attendant.done(250)) {
    say("Scram Node.js!");
    attendant.scram();
    if (!attendant.done(500)) {
      say("Node.js still running!");
    }
  }
  say("Shutdown.");
}

char* OSCALL NP_GetMIMEDescription() {
  return "application/mozilla-npruntime-scriptable-plugin:.foo:Scriptability";
}
