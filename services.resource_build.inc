<?php

/**
 * @file
 *  Contains functions necessary for building the resource definitions. This
 *  is only needed the first time a the resources for a endpoint are fetched,
 *  or when the cache has been cleared.
 */

/**
 * Builds the resource definition array for a endpoint.
 *
 * @param string $endpoint_name
 *  Optional. The endpoint name.
 * @return array
 *  The resource definitions.
 */
function _services_build_resources($endpoint_name = '') {
  module_load_include('resource_runtime.inc', 'services');

  // Get all installed resources
  $resources = module_invoke_all('services_resources');

  // Load the endpoint
  $endpoint = NULL;
  if (!empty($endpoint_name)) {
    $endpoint = services_endpoint_load($endpoint_name);
    // Apply the endpoint on the services
    _services_apply_endpoint($resources, $endpoint, TRUE);
  }

  drupal_alter('services_resources', $resources, $endpoint);

  // Process the resources, and collect all controllers in the process
  $controllers = array();
  foreach ($resources as $name => &$resource) {
    _services_process_resource($name, $resource, $controllers);
  }

  // Make sure that we got a access callback for all resources
  foreach ($controllers as &$controller) {
    if (!empty($controller['file'])) {
      // Convert old-style file references to something that fits module_load_include() better.
      if (!empty($controller['file']['file']) && empty($controller['file']['type'])) {
        $controller['file']['type'] = $controller['file']['file'];
      }
    }
    if (!isset($controller['access callback'])) {
      $controller['access callback'] = 'user_access';
    }
  }
  drupal_alter('services_resources_controller_post_processing', $controllers, $endpoint);
  drupal_alter('services_resources_post_processing', $resources, $endpoint);

  // Do some endpoint-dependent processing
  if ($endpoint) {
    // Let the authentication modules alter our controllers
    foreach ($endpoint->authentication as $auth_module => $auth_settings) {
      services_auth_invoke($auth_module, 'alter_controllers', $auth_settings, $controllers, $endpoint);
    }

    // Apply any aliases from endpoint
    if (!empty($endpoint)) {
      $aliased = array();
    }
    foreach ($resources as $key => $def) {
      if (!empty($def['endpoint']['alias'])) {
        $aliased[$def['endpoint']['alias']] = $def;
      }
      else {
        $aliased[$key] = $def;
      }
    }
    $resources = $aliased;
  }

  return $resources;
}

/**
 * Applies the endpoint to a set of resources. Resources and controllers that
 * aren't supported will be removed (if $strict is set to TRUE) and both
 * resources and controllers will get the 'endpoint' attribute set.
 *
 * @param array $resources
 *  An array of resources that the endpoint should be applied on.
 * @param array $endpoint
 *  A endpoint information array.
 * @param bool $strict
 *  Optional.
 * @return void
 */
function _services_apply_endpoint(&$resources, $endpoint, $strict = TRUE) {
  foreach ($resources as $name => &$resource) {
    $cres = ($endpoint && isset($endpoint->resources[$name])) ? $endpoint->resources[$name] : array();
    $resource['endpoint'] = $cres;

    if ($strict && empty($cres)) {
      unset($resources[$name]);
    }
    else {
      $crud = array('create', 'retrieve', 'update', 'delete', 'index');
      foreach ($crud as $op) {
        if (isset($resource[$op])) {
          $cop = isset($cres['operations'][$op]) ? $cres['operations'][$op] : array();
          $resource[$op]['endpoint'] = $cop;
          if ($strict && (empty($cop) || !$cop['enabled'])) {
            unset($resource[$op]);
          }
        }
      }

      $classes = array('targeted actions', 'actions', 'relationships');
      foreach ($classes as $class) {
        if (!empty($resource[$class])) {
          foreach ($resource[$class] as $op => $def) {
            $cop = isset($cres[$class][$op]) ? $cres[$class][$op] : array();
            if (empty($cop) || !$cop['enabled']) {
              if ($strict) {
                unset($resource[$class][$op]);
              }
            }
            else {
              $resource[$class][$op]['endpoint'] = $cop;
            }
          }
        }
      }
    }
  }
}

/**
 * Process resource runs through all the controllers of a resource and applies
 * some inheritance logic and adds the to the $controllers array.
 *
 * @param string $name
 *  The name of the resource
 * @param array &$resource
 *  The resource definition
 * @param array &$controllers
 *  An that will be fillew with all the controllers for the resource.
 * @return void
 */
function _services_process_resource($name, &$resource, &$controllers) {
  $resource['name'] = $name;

  $keys = array('retrieve', 'create', 'update', 'delete');
  foreach ($keys as $key) {
    if (isset($resource[$key])) {
      $controllers[$name . '/' . $key] = &$resource[$key];
    }
  }

  if (isset($resource['index'])) {
    $controllers[$name . '/index'] = &$resource['index'];
  }

  if (isset($resource['relationships'])) {
    foreach ($resource['relationships'] as $relname => $rel) {
      // Run some inheritance logic
      if (isset($resource['retrieve'])) {
        if (empty($rel['args']) || $rel['args'][0]['name'] !== $resource['retrieve']['args'][0]['name']) {
          array_unshift($rel['args'], $resource['retrieve']['args'][0]);
        }
        $resource['relationships'][$relname] = array_merge($resource['retrieve'], $rel);
      }
      $controllers[$name . '/relationship/' . $relname] = &$resource['relationships'][$relname];
    }
  }

  if (isset($resource['actions'])) {
    foreach ($resource['actions'] as $actname => $act) {
      // Run some inheritance logic
      if (isset($resource['update'])) {
        $up = $resource['update'];
        unset($up['args']);
        $resource['actions'][$actname] = array_merge($up, $act);
      }
      $controllers[$name . '/action/' . $actname] = &$resource['actions'][$actname];
    }
  }

  if (isset($resource['targeted actions'])) {
    foreach ($resource['targeted actions'] as $actname => $act) {
      // Run some inheritance logic
      if (isset($resource['update'])) {
        if (empty($act['args']) || $act['args'][0]['name'] !== $resource['update']['args'][0]['name']) {
          array_unshift($act['args'], $resource['update']['args'][0]);
        }
        $resource['targeted actions'][$actname] = array_merge($resource['update'], $act);
      }
      $controllers[$name . '/targeted_action/' . $actname] = &$resource['actions'][$actname];
    }
  }
}

/**
 * Supplies the resource definitions for Drupal core data
 *
 * @return array
 */
function _services_core_resources() {
  require_once("resources/comment_resource.inc");
  require_once("resources/file_resource.inc");
  require_once("resources/node_resource.inc");
  require_once("resources/system_resource.inc");
  require_once("resources/taxonomy_resource.inc");
  require_once("resources/user_resource.inc");

  $resources = array();

  $resources += _comment_resource_definition();
  $resources += _file_resource_definition();
  $resources += _node_resource_definition();
  $resources += _system_resource_definition();
  $resources += _taxonomy_resource_definition();
  $resources += _user_resource_definition();

  return $resources;
}

/**
 * Invokes all hook_service and translates them to actions on resources.
 *
 * @return array
 */
function _services_legacy_services_as_resources() {
  $services = $methods = module_invoke_all('service');
  $resources = array();

  foreach ($services as $service) {
    $signature = preg_split('/\./', $service['method']);

    $controller = $service;
    $controller['args'] = array();

    if (!empty($service['args'])) {
      foreach ($service['args'] as $arg) {
        $arg['source'] = array(
          'data' => $arg['name'],
        );
        $controller['args'][] = $arg;
      }
    }

    $resources['service_' . $signature[0]]['actions'][$signature[1]] = $controller;
  }
  return $resources;
}