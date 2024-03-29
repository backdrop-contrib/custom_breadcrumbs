<?php

/**
 * @file
 * Helper functions for custom_breadcrumbs_taxonomy.
 */

/**
 * Finds all terms associated with a node.
 *
 */
function custom_breadcrumbs_taxonomy_node_get_terms($node, $key = 'tid') {
  static $terms;
  if (isset($node->nid) && isset($node->vid)) {
    if (!isset($terms[$node->vid][$key])) {
      $query = db_select('taxonomy_index', 'r');
      $t_alias = $query->join('taxonomy_term_data', 't', 'r.tid = t.tid');
      $query->fields($t_alias);
      $query->condition("r.nid", $node->nid);
      $query->orderBy('t.weight');
      $query->orderBy('t.name');
      $result = $query->execute();
      $terms[$node->vid][$key] = array();
      foreach ($result as $term) {
        $terms[$node->vid][$key][$term->$key] = $term;
      }
    }
    // Allow other modules to alter the terms found by the previous query.
    backdrop_alter('custom_breadcrumbs_taxonomy_node_terms', $terms, $node, $key);
    return $terms[$node->vid][$key];
  }
  return array();
}

/**
 * Sets the breadcrumb using a node's taxonomy.
 *
 * @param string $tid
 *   A taxonomy term id.
 * @param string $vocabulary
 *   A taxonomy vocabulary name.
 * @param bool $is_term_page
 *   TRUE if the breadcrumb is being prepared for the taxonomy term page, FALSE
 *   otherwise.
 * @param array $objs
 *   An optional array of objects to be used for token replacement.
 * @param array $terms
 *   An array of taxonomy terms to use (potentially) to construct the
 *   breadcrumb.
 *
 * @codingStandardsIgnoreStart
 */
function _custom_breadcrumbs_taxonomy_set_breadcrumb($tid, $vocabulary = NULL, $is_term_page = FALSE, $objs = array(), $terms = array()) {
  // @codingStandardsIgnoreEnd
  if (isset($_SESSION['custom_breadcrumbs_taxonomy_set'])) {
    unset($_SESSION['custom_breadcrumbs_taxonomy_set']);
  }
  if (config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_use_hierarchy') && (!custom_breadcrumbs_exclude_path())) {
    $breadcrumb = custom_breadcrumbs_taxonomy_generate_breadcrumb($tid, $vocabulary, $is_term_page, $objs);
    if ($is_term_page) {
      _custom_breadcrumbs_taxonomy_recent_term($tid);
    }
    if (config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_include_node_title') && isset($objs['node'])) {
      $breadcrumb[] = check_plain($objs['node']->title);
    }
    backdrop_set_breadcrumb($breadcrumb);
    custom_breadcrumbs_taxonomy_set_session();

    // Optionally save the unique breadcrumb id of the last set breadcrumb.
    custom_breadcrumbs_unique_breadcrumb_id('taxonomy');
  }
  else {
    global $language;
    $languages = array(
      'language' => $language->langcode,
      'all' => '',
    );
    // Check each term to see if it has a custom breadcrumb.
    $vocabularies = array();
    if (!empty($terms)) {
      foreach ($terms as $term) {
        $breadcrumbs = custom_breadcrumbs_load_breadcrumbs('custom_breadcrumbs_taxonomy', 'custom_breadcrumbs_taxonomy_term', array('tid' => $term->tid), $languages);
        $objs['taxonomy_term'] = $term;
        if ($breadcrumb = custom_breadcrumbs_select_breadcrumb($breadcrumbs, $objs)) {
          if (custom_breadcrumbs_set_breadcrumb($breadcrumb, $objs)) {
            custom_breadcrumbs_taxonomy_set_session();
          }
          _custom_breadcrumbs_taxonomy_recent_term($term->tid);
          return;
        }
        if (!isset($vocabularies[$term->vocabulary])) {
          $vocabularies[$term->vocabulary] = $term;
        }
      }
    }
    if (empty($vocabularies) && !is_null($vocabulary)) {
      $vocabularies[$vocabulary] = NULL;
    }
    // No terms match, look for a match on the taxonomy vocabulary.
    foreach ($vocabularies as $vocabulary_name => $term) {
      $breadcrumbs = custom_breadcrumbs_load_breadcrumbs('custom_breadcrumbs_taxonomy', 'custom_breadcrumbs_taxonomy_vocabulary', array('vocabulary' => $vocabulary_name), $languages);
      $objs['taxonomy_term'] = $term;
      if ($breadcrumb = custom_breadcrumbs_select_breadcrumb($breadcrumbs, $objs)) {
        if (custom_breadcrumbs_set_breadcrumb($breadcrumb, $objs)) {
          custom_breadcrumbs_taxonomy_set_session();
        }
        return;
      }
    }
  }
}

/**
 * Returns the previous selected term or the lightest term for a given node.
 *
 * @param object $node
 *   The node object.
 *
 * @return object
 *   The taxonomy term object.
 */
function custom_breadcrumbs_taxonomy_node_get_term($node) {
  // First try to see if a recently viewed term matches one of the node's terms.
  $term = custom_breadcrumbs_taxonomy_node_get_recent_term($node);
  // If not, then select the nodes lightest term.
  if (is_null($term)) {
    $term = custom_breadcrumbs_taxonomy_node_get_lightest_term($node);
  }
  return $term;
}

/**
 * Returns the most recently selected term for a given node.
 *
 * @param object $node
 *   The node object.
 *
 * @return string
 *   The previously selected taxonomy term object that also belongs to the node.
 */
function custom_breadcrumbs_taxonomy_node_get_recent_term($node) {
  $terms = custom_breadcrumbs_taxonomy_node_get_terms($node);
  $tid = _custom_breadcrumbs_taxonomy_recent_term();
  if (is_array($terms) && !empty($terms) && !is_null($tid)) {
    return (isset($terms[$tid])) ? taxonomy_term_load($tid) : NULL;
  }
}

/**
 * Sets or returns the previous selected term id.
 *
 * @param string $tid
 *   An optional term id to store in the session variable to establish a term
 *   history.
 *
 * @return string
 *   If this function is called without a term id, then it will return the
 *   previously selected taxonomy term id, retrieved from the session variable.
 */
function _custom_breadcrumbs_taxonomy_recent_term($tid = NULL) {
  global $user;
  if (config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_session') &&
      !(($user->uid == 0) && config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_no_anon_sessions'))) {
    if (!is_null($tid)) {
      $_SESSION['custom_breadcrumbs_previous_term'] = $tid;
    }
    elseif (isset($_SESSION['custom_breadcrumbs_previous_term'])) {
      return $_SESSION['custom_breadcrumbs_previous_term'];
    }
  }
}

/**
 * Returns the lightest term for a given node.
 *
 * If the term has parents, then the lightest parent's weight is used for the
 * term weight. And if the parent has multiple child terms at different depths,
 * the deepest child term will be returned. If the child terms have the same
 * depth, then the lightest child term is returned.
 *
 * @param object $node
 *   The node object.
 *
 * @return object
 *   The taxonomy term object.
 */
function custom_breadcrumbs_taxonomy_node_get_lightest_term($node) {
  $terms = custom_breadcrumbs_taxonomy_node_get_terms($node);
  if (!empty($terms)) {
    if (count($terms) > 1) {
      foreach ($terms as $term) {
        // Only consider terms in the lightest vocabulary.
        // @codingStandardsIgnoreLine
        if (!isset($vocabulary)) {
          $vocabulary = $term->vocabulary;
        }
        elseif ($term->vocabulary != $vocabulary) {
          continue;
        }
        // If the term has parents, the weight of the term is the weight of the
        // lightest parent.
        $parents = taxonomy_get_parents_all($term->tid);
        $depth = count($parents);
        if ($depth > 0) {
          $parent = array_pop($parents);
          $weight = $parent->weight;
        }
        else {
          $weight = $term->weight;
        }
        // @codingStandardsIgnoreLine
        if ((isset($lweight) && ($weight < $lweight)) || !isset($lweight)) {
          $lterm = $term;
          $lweight = $weight;
          $ldepth = $depth;
        }
        elseif (isset($lweight) && ($weight == $lweight)) {
          // If the node has multiple child terms with the same parent, choose
          // the child with the greatest depth.
          if ($depth > $ldepth) {
            $lterm = $term;
            $ldepth = $depth;
          }
          elseif ($depth == $ldepth) {
            // If the terms have the same depth, pick the term with the lightest
            // weight.
            $lterm = ($lterm->weight < $term->weight) ? $lterm : $term;
          }
        }
      }
      return $lterm;
    }
    else {
      return array_pop($terms);
    }
  }
}

/**
 * Generate Breadcrumb.
 *
 * Generates a breadcrumb from the taxonomy hierarchy of the term id or vocab
 * id.
 * This will only be called if custom_breadcrumbs_taxonomy_use_hierarchy has
 * been enabled.
 *
 * @param string $tid
 *   A taxonomy term id.
 * @param string $vocabulary
 *   A taxonomy vocabulary name.
 * @param bool $is_term_page
 *   TRUE if the breadcrumb is being prepared for the taxonomy term page, FALSE
 *   otherwise.
 * @param array $objs
 *   An optional array of objects to be used for token replacement.
 *
 * @return array
 *   The breadcrumb trail.
 *
 * @codingStandardsIgnoreStart
 */
function custom_breadcrumbs_taxonomy_generate_breadcrumb($tid, $vocabulary = NULL, $is_term_page = FALSE, $objs = NULL) {
  // @codingStandardsIgnoreEnd
  if ($is_term_page && isset($objs['taxonomy'])) {
    $term = $objs['taxonomy'];
  }
  else {
    $term = (is_null($tid)) ? NULL : taxonomy_term_load($tid);
    $objs['taxonomy'] = $term;
  }
  $vocabid = (!is_null($vocabulary)) ? $vocabulary : (is_null($term) ? NULL : $term->vocabulary);
  $types = custom_breadcrumbs_token_types($objs);

  $trail = array();

  if (!in_array($vocabid, config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_excluded_vocabs'))) {
    $trail = _custom_breadcrumbs_taxonomy_home_trail();

    if (!is_null($vocabid)) {
      $vocab_trail = _custom_breadcrumbs_taxonomy_vocabulary_trail($vocabid, $is_term_page, $objs, $types, count($trail));
      $trail = array_merge($trail, $vocab_trail);
    }

    if (!is_null($tid)) {
      $term_trail = _custom_breadcrumbs_taxonomy_term_trail($tid, $is_term_page, $objs, $types, count($trail));
      $trail = array_merge($trail, $term_trail);
      // Optionally remove the current TERM from end of breadcrumb trail.
      if ((!config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_show_current_term') || ($is_term_page && !config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_show_current_term_term'))) && (count($trail) > 1)) {
        array_pop($trail);
      }
    }
  }
  return $trail;
}

/**
 * Translates configurable string when i18n_string module is installed.
 *
 * @param string $name
 *   The name of the string.
 * @param string $default
 *   String in default language.
 * @param string $langcode
 *   The language code for translation if not the one used for the page.
 *
 * @return string
 *   The translated string if possible, or the original string if not.
 */
function _custom_breadcrumbs_taxonomy_tt($name, $default, $langcode = NULL) {
  return module_exists('i18n_string') ? i18n_string($name, $default, array('langcode' => $langcode)) : $default;
}

/**
 * Generates the home breadcrumb trail.
 *
 * @return array
 *   The breadcrumb trail.
 */
function _custom_breadcrumbs_taxonomy_home_trail() {
  $trail = array();
  if (config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_append_breadcrumb')) {
    // Check to see if a breadcrumb has already been defined.
    $trail = backdrop_get_breadcrumb();
  }
  else {
    $trail = custom_breadcrumbs_home_crumb();
  }
  return $trail;
}

/**
 * Generates the vocabulary trail.
 *
 * @param string $vocabulary
 *   A taxonomy vocabulary name.
 * @param bool $is_term_page
 *   TRUE if the breadcrumb is being prepared for the taxonomy term page, FALSE
 *   otherwise.
 * @param array $objs
 *   An optional array of objects to be used to determine breadcrumb visibility
 *   and for token replacement.
 * @param array $types
 *   An array of token types to be used in token replacement.
 * @param int $part
 *   A positive integer indicating the breadcrumb segment (home crumb = 0).
 *
 * @return array
 *   The breadcrumb trail.
 *
 * @codingStandardsIgnoreStart
 */
function _custom_breadcrumbs_taxonomy_vocabulary_trail($vocabulary, $is_term_page = FALSE, $objs = array(), $types = array('global' => NULL), $part = 1) {
  // @codingStandardsIgnoreEnd
  // Generate the VOCABULARY breadcrumb.
  $trail = array();
  // Check to see if a vocabulary breadcrumb exists.
  $breadcrumbs = custom_breadcrumbs_load_breadcrumbs('custom_breadcrumbs_taxonomy', 'custom_breadcrumbs_taxonomy_vocabulary', array('vocabulary' => $vocabulary));
  $vocabulary_path = NULL;
  $title = NULL;
  $bid = NULL;
  if ($breadcrumb = custom_breadcrumbs_select_breadcrumb($breadcrumbs, $objs)) {
    $vocabulary_path = $breadcrumb->paths;
    $title = $breadcrumb->titles;
    $bid = $breadcrumb->bid;
    $vocabulary_path = token_replace($vocabulary_path, $types, array('clear' => TRUE));
    $title = token_replace($title, $types, array('clear' => TRUE));
  }
  if ($title == NULL) {
    $vocabulary_entity = taxonomy_vocabulary_load($vocabulary);
    $title = _custom_breadcrumbs_taxonomy_tt(array(
      'taxonomy',
      'vocabulary',
      $vocabulary_entity,
      'name',
    ), $vocabulary);
  }
  if ($vocabulary_path != NULL) {
    $options = _custom_breadcrumbs_identifiers_option($part, $bid);
    $trail = array(l($title, $vocabulary_path, $options));
  }
  elseif (config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_show_vocabulary')) {
    $trail = array(check_plain($title));
  }
  return $trail;
}

/**
 * Generates the taxonomy term trail.
 *
 * @param string $tid
 *   A taxonomy term id.
 * @param bool $is_term_page
 *   TRUE if the breadcrumb is being prepared for the taxonomy term page, FALSE
 *   otherwise.
 * @param array $objs
 *   An optional array of objects to be used to determine breadcrumb visibility
 *   and for token replacement.
 * @param array $types
 *   An array of token types to be used in token replacement.
 * @param int $part
 *   A positive integer indicating the breadcrumb segment (home crumb = 0).
 *
 * @return array
 *   The breadcrumb trail.
 *
 * @codingStandardsIgnoreStart
 */
function _custom_breadcrumbs_taxonomy_term_trail($tid, $is_term_page = FALSE, $objs = array(), $types = array('global' => NULL), $part = 1) {
  // @codingStandardsIgnoreEnd
  // Generate the TERM breadcrumb.
  $trail = array();
  $parent_terms = array_reverse(taxonomy_get_parents_all($tid));
  foreach ($parent_terms as $parent_term) {
    $breadcrumbs = custom_breadcrumbs_load_breadcrumbs('custom_breadcrumbs_taxonomy', 'custom_breadcrumbs_taxonomy_term', array('tid' => $parent_term->tid));
    $term_path = NULL;
    $title = NULL;
    $bid = NULL;
    if ($breadcrumb = custom_breadcrumbs_select_breadcrumb($breadcrumbs, $objs)) {
      $term_path = $breadcrumb->paths;
      $title = $breadcrumb->titles;
      $bid = $breadcrumb->bid;
      $term_path = token_replace($term_path, $types, array('clear' => TRUE));
      $title = token_replace($title, $types, array('clear' => TRUE));
    }
    if ($title == NULL) {
      $title = _custom_breadcrumbs_taxonomy_tt(array(
        'taxonomy',
        'term',
        $parent_term->tid,
        'name',
      ), $parent_term->name);
    }
    if ($term_path == NULL) {
      $uri = entity_uri('taxonomy_term', $parent_term);
      $term_path = $uri['path'];
    }

    // Do not create links to own self if we are on a taxonomy/term page.
    if ($is_term_page && $parent_term->tid == $tid) {
      $trail[] = check_plain($title);
    }
    else {
      $options = _custom_breadcrumbs_identifiers_option($part, $bid);
      $trail[] = l($title, $term_path, $options);
    }
    ++$part;
  }
  return $trail;
}

/**
 * Set Session.
 *
 * Optionally, set a session variable to indicate that a custom breadcrumbs
 * taxonomy breadcrumb has been set.
 */
function custom_breadcrumbs_taxonomy_set_session() {
  global $user;
  if (config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_session') && !(($user->uid == 0) && config_get('custom_breadcrumbs.settings', 'custom_breadcrumbs_taxonomy_no_anon_sessions'))) {
    $_SESSION['custom_breadcrumbs_taxonomy_set'] = TRUE;
  }
}
