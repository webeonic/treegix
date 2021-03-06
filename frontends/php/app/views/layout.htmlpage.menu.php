<?php


$user_navigation = (new CList())
    ->addClass(TRX_STYLE_TOP_NAV_ICONS)
    ->addItem(
        (new CForm('get', 'treegix.php'))
            ->cleanItems()
            ->addItem([
                (new CVar('action', 'search'))->removeId(),
                (new CTextBox('search', getRequest('search', ''), false, 255))
                    ->setAttribute('autocomplete', 'off')
                    ->addClass(TRX_STYLE_SEARCH)
                    ->setAttribute('aria-label', _('type here to search')),
                (new CSubmitButton('&nbsp;'))
                    ->addClass(TRX_STYLE_BTN_SEARCH)
                    ->setTitle(_('Search'))
            ])
            ->setAttribute('role', 'search')
    );

$user_menu = (new CList())
    ->setAttribute('role', 'navigation')
    ->setAttribute('aria-label', _('User menu'))
    ->addItem(CBrandHelper::isRebranded()
        ? null
        : null
    )
    ->addItem(CBrandHelper::isRebranded()
        ? null
        : null
    );


$user_menu->addItem(
    (new CLink(SPACE, 'javascript:;'))
        ->addClass(TRX_STYLE_TOP_NAV_SIGNOUT)
        ->setTitle(_('Sign out'))
        ->onClick('TREEGIX.logout()')
);

$user_navigation->addItem($user_menu);

// 1st level menu
$top_menu = (new CDiv())
    ->addItem(
        (new CLink(
            (new CDiv())
                ->addClass(TRX_STYLE_LOGO)
                ->addStyle(CBrandHelper::getLogoStyle()),
            'treegix.php?action=dashboard.view'
        ))
            ->addClass(TRX_STYLE_HEADER_LOGO)
    )
    ->addItem(
        (new CTag('ul', true, (new CList($data['menu']['main_menu']))->addClass(TRX_STYLE_TOP_NAV)))
            ->setAttribute('aria-label', _('Main navigation'))
    )
    ->addItem($user_navigation)
    ->addClass(TRX_STYLE_TOP_NAV_CONTAINER)
    ->setId('mmenu');

$sub_menu_div = (new CTag('nav', true))
    ->setAttribute('aria-label', _('Sub navigation'))
    ->addClass(TRX_STYLE_TOP_SUBNAV_CONTAINER)
    ->onMouseover('javascript: MMenu.submenu_mouseOver();')
    ->onMouseout('javascript: MMenu.mouseOut();');

// 2nd level menu
foreach ($data['menu']['sub_menus'] as $label => $sub_menu) {
    $sub_menu_row = (new CList())
        ->addClass(TRX_STYLE_TOP_SUBNAV)
        ->setId('sub_' . $label);

    foreach ($sub_menu as $id => $sub_page) {
        $url = new CUrl($sub_page['menu_url']);
        if ($sub_page['menu_action'] !== null) {
            $url->setArgument('action', $sub_page['menu_action']);
        }

        $url
            ->setArgument('ddreset', 1)
            ->removeArgument('sid');

        $sub_menu_item = (new CLink($sub_page['menu_text'], $url->getUrl()))->setAttribute('tabindex', 0);
        if ($sub_page['selected']) {
            $sub_menu_item->addClass(TRX_STYLE_SELECTED);
        }

        $sub_menu_row->addItem($sub_menu_item);
    }

    if ($data['menu']['selected'] === $label) {
        $sub_menu_row->setAttribute('style', 'display: block;');
        insert_js('MMenu.def_label = ' . trx_jsvalue($label));
    } else {
        $sub_menu_row->setAttribute('style', 'display: none;');
    }
    $sub_menu_div->addItem($sub_menu_row);
}

if ($data['server_name'] !== '') {
    $sub_menu_div->addItem(
        (new CDiv($data['server_name']))->addClass(TRX_STYLE_SERVER_NAME)
    );
}

(new CTag('header', true))
    ->addItem(
        (new CDiv())
            ->addItem($top_menu)
            ->addItem($sub_menu_div)
    )
    ->show();
